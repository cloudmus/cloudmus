#include "SmtcService.h"

#include "SmtcAbiWin.h"

#include <QBuffer>
#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QTimer>

#include <windows.h>

#include <objidl.h>
#include <roapi.h>
#include <shcore.h> // CreateRandomAccessStreamOverStream
#include <shlwapi.h> // SHCreateMemStream
#include <winstring.h>
#include <wrl/client.h>

#include <systemmediatransportcontrolsinterop.h>

#include <algorithm>
#include <atomic>
#include <functional>

#include "CoverArtCache.h"
#include "Models.h"
#include "PlaybackController.h"

using Microsoft::WRL::ComPtr;

namespace Integration {

namespace {

namespace Abi = Smtc::Abi;

// Deliberately not the system headers' DEFINE_GUID'd IID_IUnknown /
// IID_IAgileObject: those need actual storage from a lib we don't
// otherwise link (uuid.lib's mingw equivalent). Self-contained instead,
// same treatment as everything in SmtcAbiWin.h.
constexpr GUID kIidUnknown = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
constexpr GUID kIidAgileObject = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
// The interop class factory's own interface — see systemmediatransportcontrolsinterop.h
// (its DEFINE_GUID'd IID_ISystemMediaTransportControlsInterop has the same
// link-storage caveat).
constexpr GUID kIidSmtcInterop = { 0xddb0472d, 0xc911, 0x4a1f, { 0x86, 0xd9, 0xdc, 0x3d, 0x71, 0xa9, 0x5f, 0x5a } };

// An HSTRING that owns its buffer for as long as it's alive.
class HStringHolder {
public:
    explicit HStringHolder(const QString& s)
    {
        WindowsCreateString(reinterpret_cast<const wchar_t*>(s.utf16()), static_cast<UINT32>(s.length()), &value_);
    }
    ~HStringHolder()
    {
        if (value_)
            WindowsDeleteString(value_);
    }
    HStringHolder(const HStringHolder&) = delete;
    HStringHolder& operator=(const HStringHolder&) = delete;
    HSTRING get() const { return value_; }

private:
    HSTRING value_ = nullptr;
};

// A one-off IUnknown implementing exactly one TypedEventHandler<SystemMediaTransportControls,
// Args>, forwarding to a std::function on whatever thread the OS calls
// Invoke() from (a WinRT thread-pool thread, never our own) — callers hop
// to the Qt thread themselves before touching PlaybackController. Answers
// QueryInterface for IUnknown, IAgileObject (so no per-apartment proxy is
// needed for in-process use) and its own parameterized-interface id.
template <typename Args>
class TypedHandler : public IUnknown {
public:
    using Callback = std::function<void(Args*)>;

    TypedHandler(const GUID& iid, Callback callback)
        : iid_(iid)
        , callback_(std::move(callback))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (IsEqualGUID(riid, kIidUnknown) || IsEqualGUID(riid, kIidAgileObject) || IsEqualGUID(riid, iid_)) {
            *ppv = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ref_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    // The 4th vtable slot, right after QueryInterface/AddRef/Release — what
    // makes this binary-compatible with TypedEventHandler<SystemMediaTransportControls,
    // Args>: sender is unused (its static type would be
    // ISystemMediaTransportControls*, we never need it).
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable* /*sender*/, Args* args)
    {
        callback_(args);
        return S_OK;
    }

private:
    GUID iid_;
    Callback callback_;
    std::atomic<ULONG> ref_ { 1 };
};

} // namespace

class SmtcService::Impl {
public:
    Impl(Playback::PlaybackController& playback, Covers::CoverArtCache& covers, SmtcService* owner)
        : playback_(playback)
        , covers_(covers)
        , owner_(owner)
    {
        if (!createHiddenWindow())
            return;
        if (!initSmtc())
            return;

        positionTimer_ = new QTimer(owner_);
        positionTimer_->setInterval(5000);
        QObject::connect(positionTimer_, &QTimer::timeout, owner_, [this]() { updateTimelinePosition(); });

        QObject::connect(&playback_, &Playback::PlaybackController::trackChanged, owner_,
            [this](const Track&, const QString&) { updateTrack(); });
        QObject::connect(
            &playback_, &Playback::PlaybackController::currentTrackAvailabilityChanged, owner_, [this](bool available) {
                if (!available)
                    updateTrack();
            });
        QObject::connect(&playback_, &Playback::PlaybackController::playingChanged, owner_, [this](bool) {
            updatePlaying();
            updateTimelinePosition();
        });
        QObject::connect(
            &playback_, &Playback::PlaybackController::seeked, owner_, [this](qint64) { updateTimelinePosition(); });
        QObject::connect(
            &playback_, &Playback::PlaybackController::playModeChanged, owner_, [this]() { updatePlayMode(); });
        QObject::connect(
            &covers_, &Covers::CoverArtCache::pixmapReady, owner_, [this](const QString& url) { onCoverReady(url); });

        if (playback_.hasCurrentTrack())
            updateTrack();
        updatePlaying();
        updatePlayMode();
    }

    ~Impl()
    {
        if (smtc2_) {
            if (positionHandlerOk_)
                smtc2_->remove_PlaybackPositionChangeRequested(positionToken_);
            if (shuffleHandlerOk_)
                smtc2_->remove_ShuffleEnabledChangeRequested(shuffleToken_);
            if (repeatHandlerOk_)
                smtc2_->remove_AutoRepeatModeChangeRequested(repeatToken_);
        }
        if (smtc_ && buttonHandlerOk_)
            smtc_->remove_ButtonPressed(buttonToken_);
        if (hwnd_)
            DestroyWindow(hwnd_);
        if (classRegistered_)
            UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

private:
    static constexpr wchar_t kWindowClassName[] = L"CloudMusSmtcHiddenWindow";

    bool createHiddenWindow()
    {
        WNDCLASSW wc { };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClassName;
        if (RegisterClassW(&wc)) {
            classRegistered_ = true;
        } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            qWarning() << "Smtc: RegisterClassW failed" << GetLastError();
            return false;
        }
        // A real top-level window, just never shown (no WS_VISIBLE, no
        // ShowWindow() call) — not HWND_MESSAGE: SMTC's GetForWindow()
        // rejects a message-only window with E_INVALIDARG, since it ties
        // sessions to the normal top-level window manager (foreground,
        // restore-from-minimized, ...), which a message-only window has
        // no place in.
        // WS_EX_TOOLWINDOW: never shown either way, but belt-and-braces —
        // it must never be a candidate for a taskbar button of its own.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) {
            qWarning() << "Smtc: CreateWindowExW failed" << GetLastError();
            return false;
        }
        return true;
    }

    bool initSmtc()
    {
        // Fine to call even if Qt already put the thread in STA (it will
        // have, for OLE drag & drop): RoInitialize is ref-counted per
        // thread and layered directly over CoInitializeEx.
        RoInitialize(RO_INIT_SINGLETHREADED);

        ComPtr<::ISystemMediaTransportControlsInterop> interop;
        {
            HStringHolder className(QStringLiteral("Windows.Media.SystemMediaTransportControls"));
            const HRESULT hr = RoGetActivationFactory(
                className.get(), kIidSmtcInterop, reinterpret_cast<void**>(interop.GetAddressOf()));
            if (FAILED(hr) || !interop) {
                qWarning() << "Smtc: RoGetActivationFactory(SystemMediaTransportControls) failed" << Qt::hex << hr;
                return false;
            }
        }

        HRESULT hr = interop->GetForWindow(
            hwnd_, Abi::IID_ISystemMediaTransportControls, reinterpret_cast<void**>(smtc_.GetAddressOf()));
        if (FAILED(hr) || !smtc_) {
            qWarning() << "Smtc: GetForWindow failed" << Qt::hex << hr;
            return false;
        }

        // Shuffle/repeat/seek-request live on interface 2; older systems
        // without it just don't get that slice (checked at every use).
        smtc_->QueryInterface(Abi::IID_ISystemMediaTransportControls2, reinterpret_cast<void**>(smtc2_.GetAddressOf()));

        smtc_->put_IsEnabled(TRUE);
        smtc_->put_IsPlayEnabled(TRUE);
        smtc_->put_IsPauseEnabled(TRUE);
        smtc_->put_IsStopEnabled(TRUE);
        smtc_->put_IsNextEnabled(TRUE);
        smtc_->put_IsPreviousEnabled(TRUE);

        hr = smtc_->get_DisplayUpdater(displayUpdater_.GetAddressOf());
        if (FAILED(hr) || !displayUpdater_) {
            qWarning() << "Smtc: get_DisplayUpdater failed" << Qt::hex << hr;
            return false;
        }
        displayUpdater_->put_Type(Abi::MediaPlaybackType::Music);

        registerHandlers();
        // Nothing above warns on success, so without this line a working
        // run and a run that silently never reached initSmtc() at all
        // (e.g. a stale build) look identical in the log.
        qInfo() << "Smtc: registered with the system" << "control2=" << bool(smtc2_)
                << "buttonHandler=" << buttonHandlerOk_;
        return true;
    }

    void registerHandlers()
    {
        auto* buttonHandler = new TypedHandler<Abi::ISystemMediaTransportControlsButtonPressedEventArgs>(
            Abi::IID_ButtonPressedHandler, [this](Abi::ISystemMediaTransportControlsButtonPressedEventArgs* args) {
                Abi::SystemMediaTransportControlsButton button { };
                if (FAILED(args->get_Button(&button)))
                    return;
                QMetaObject::invokeMethod(owner_, [this, button]() { onButtonPressed(button); }, Qt::QueuedConnection);
            });
        buttonHandlerOk_ = SUCCEEDED(smtc_->add_ButtonPressed(buttonHandler, &buttonToken_));
        buttonHandler->Release(); // add_ButtonPressed() AddRef'd its own copy if it wants one

        if (!smtc2_)
            return;

        auto* positionHandler = new TypedHandler<Abi::IPlaybackPositionChangeRequestedEventArgs>(
            Abi::IID_PlaybackPositionChangeRequestedHandler,
            [this](Abi::IPlaybackPositionChangeRequestedEventArgs* args) {
                Abi::TimeSpan requested { };
                if (FAILED(args->get_RequestedPlaybackPosition(&requested)))
                    return;
                const qint64 positionMs = requested.Duration / 10000;
                QMetaObject::invokeMethod(
                    owner_, [this, positionMs]() { onSeekRequested(positionMs); }, Qt::QueuedConnection);
            });
        positionHandlerOk_ = SUCCEEDED(smtc2_->add_PlaybackPositionChangeRequested(positionHandler, &positionToken_));
        positionHandler->Release();

        auto* shuffleHandler = new TypedHandler<Abi::IShuffleEnabledChangeRequestedEventArgs>(
            Abi::IID_ShuffleEnabledChangeRequestedHandler, [this](Abi::IShuffleEnabledChangeRequestedEventArgs* args) {
                boolean requested = FALSE;
                if (FAILED(args->get_RequestedShuffleEnabled(&requested)))
                    return;
                const bool on = requested != FALSE;
                QMetaObject::invokeMethod(owner_, [this, on]() { playback_.setShuffle(on); }, Qt::QueuedConnection);
            });
        shuffleHandlerOk_ = SUCCEEDED(smtc2_->add_ShuffleEnabledChangeRequested(shuffleHandler, &shuffleToken_));
        shuffleHandler->Release();

        auto* repeatHandler = new TypedHandler<Abi::IAutoRepeatModeChangeRequestedEventArgs>(
            Abi::IID_AutoRepeatModeChangeRequestedHandler, [this](Abi::IAutoRepeatModeChangeRequestedEventArgs* args) {
                Abi::MediaPlaybackAutoRepeatMode requested { };
                if (FAILED(args->get_RequestedAutoRepeatMode(&requested)))
                    return;
                QMetaObject::invokeMethod(
                    owner_, [this, requested]() { onRepeatModeRequested(requested); }, Qt::QueuedConnection);
            });
        repeatHandlerOk_ = SUCCEEDED(smtc2_->add_AutoRepeatModeChangeRequested(repeatHandler, &repeatToken_));
        repeatHandler->Release();
    }

    // --- events arriving from the system (already hopped to the Qt thread) ---

    void onButtonPressed(Abi::SystemMediaTransportControlsButton button)
    {
        switch (button) {
            case Abi::SystemMediaTransportControlsButton::Play:
                if (!playback_.isPlaying())
                    playback_.togglePause();
                break;
            case Abi::SystemMediaTransportControlsButton::Pause:
                if (playback_.isPlaying())
                    playback_.togglePause();
                break;
            case Abi::SystemMediaTransportControlsButton::Stop:
                // No dedicated "stop" concept in PlaybackController's MPRIS
                // sense either — pausing is the closest equivalent, same
                // reasoning as MprisPlayerAdaptor::Stop().
                if (playback_.isPlaying())
                    playback_.togglePause();
                break;
            case Abi::SystemMediaTransportControlsButton::Next:
                playback_.next();
                break;
            case Abi::SystemMediaTransportControlsButton::Previous:
                playback_.previous();
                break;
            default:
                break;
        }
    }

    void onSeekRequested(qint64 positionMs)
    {
        if (!playback_.hasCurrentTrack() || playback_.currentTrack().durationMs <= 0)
            return;
        playback_.seek(std::clamp<qint64>(positionMs, 0, playback_.currentTrack().durationMs));
    }

    void onRepeatModeRequested(Abi::MediaPlaybackAutoRepeatMode mode)
    {
        switch (mode) {
            case Abi::MediaPlaybackAutoRepeatMode::List:
                playback_.setRepeatMode(Playback::RepeatMode::All);
                break;
            case Abi::MediaPlaybackAutoRepeatMode::Track:
                playback_.setRepeatMode(Playback::RepeatMode::One);
                break;
            case Abi::MediaPlaybackAutoRepeatMode::None:
                playback_.setRepeatMode(Playback::RepeatMode::Off);
                break;
        }
    }

    // --- our state pushed out to the system -----------------------------

    void updateTrack()
    {
        if (!displayUpdater_)
            return;
        if (!playback_.hasCurrentTrack()) {
            displayUpdater_->ClearAll();
            smtc_->put_PlaybackStatus(Abi::MediaPlaybackStatus::Closed);
            displayUpdater_->Update();
            pendingCoverUrl_.clear();
            updateTimelineExtent();
            return;
        }

        const Track& track = playback_.currentTrack();
        ComPtr<Abi::IMusicDisplayProperties> music;
        if (SUCCEEDED(displayUpdater_->get_MusicProperties(music.GetAddressOf())) && music) {
            HStringHolder title(track.title);
            music->put_Title(title.get());

            QString artists;
            for (int i = 0; i < track.artists.size(); ++i) {
                if (i > 0)
                    artists += QStringLiteral(", ");
                artists += track.artists[i].name;
            }
            HStringHolder artist(artists);
            music->put_Artist(artist.get());

            if (track.album.has_value()) {
                ComPtr<Abi::IMusicDisplayProperties2> music2;
                if (SUCCEEDED(music->QueryInterface(
                        Abi::IID_IMusicDisplayProperties2, reinterpret_cast<void**>(music2.GetAddressOf())))
                    && music2) {
                    HStringHolder albumTitle(track.album->title);
                    music2->put_AlbumTitle(albumTitle.get());
                }
            }
        }

        updateTimelineExtent();
        displayUpdater_->Update();
        updatePlaying();

        QString coverUrl = track.coverUrl.value_or(QString());
        if (coverUrl.isEmpty() && track.album.has_value())
            coverUrl = track.album->coverUrl.value_or(QString());
        setCover(coverUrl);
    }

    void updatePlaying()
    {
        if (!smtc_)
            return;
        const bool hasTrack = playback_.hasCurrentTrack();
        smtc_->put_PlaybackStatus(!hasTrack
                ? Abi::MediaPlaybackStatus::Closed
                : (playback_.isPlaying() ? Abi::MediaPlaybackStatus::Playing : Abi::MediaPlaybackStatus::Paused));
        if (positionTimer_) {
            if (hasTrack && playback_.isPlaying())
                positionTimer_->start();
            else
                positionTimer_->stop();
        }
    }

    void updatePlayMode()
    {
        if (!smtc2_)
            return;
        smtc2_->put_ShuffleEnabled(playback_.shuffleActive() ? TRUE : FALSE);
        Abi::MediaPlaybackAutoRepeatMode mode = Abi::MediaPlaybackAutoRepeatMode::None;
        switch (playback_.effectiveRepeatMode()) {
            case Playback::RepeatMode::All:
                mode = Abi::MediaPlaybackAutoRepeatMode::List;
                break;
            case Playback::RepeatMode::One:
                mode = Abi::MediaPlaybackAutoRepeatMode::Track;
                break;
            case Playback::RepeatMode::Off:
                break;
        }
        smtc2_->put_AutoRepeatMode(mode);
    }

    bool ensureTimelineProps()
    {
        if (timelineProps_)
            return true;
        HStringHolder className(QStringLiteral("Windows.Media.SystemMediaTransportControlsTimelineProperties"));
        ComPtr<IInspectable> instance;
        const HRESULT hr = RoActivateInstance(className.get(), instance.GetAddressOf());
        if (FAILED(hr) || !instance) {
            qWarning() << "Smtc: RoActivateInstance(TimelineProperties) failed" << Qt::hex << hr;
            return false;
        }
        const HRESULT qi = instance->QueryInterface(Abi::IID_ISystemMediaTransportControlsTimelineProperties,
            reinterpret_cast<void**>(timelineProps_.GetAddressOf()));
        if (FAILED(qi) || !timelineProps_) {
            qWarning() << "Smtc: no ISystemMediaTransportControlsTimelineProperties" << Qt::hex << qi;
            return false;
        }
        return true;
    }

    // Start/end/min/max-seek — pushed on track change (or cleared when
    // there's no seekable duration, e.g. a radio track).
    void updateTimelineExtent()
    {
        if (!smtc2_ || !ensureTimelineProps())
            return;
        const bool seekable = playback_.hasCurrentTrack() && playback_.currentTrack().durationMs > 0;
        const Abi::TimeSpan durationTicks { seekable ? qint64(playback_.currentTrack().durationMs) * 10000 : 0 };
        timelineProps_->put_StartTime(Abi::TimeSpan { 0 });
        timelineProps_->put_MinSeekTime(Abi::TimeSpan { 0 });
        timelineProps_->put_EndTime(durationTicks);
        timelineProps_->put_MaxSeekTime(durationTicks);
        timelineProps_->put_Position(Abi::TimeSpan { seekable ? playback_.positionMs() * 10000 : 0 });
        smtc2_->UpdateTimelineProperties(timelineProps_.Get());
    }

    // Position alone — on seeks/play-pause, and every 5s while playing
    // (Microsoft's own guidance: don't push more often, the flyout
    // extrapolates between updates from PlaybackStatus/rate).
    void updateTimelinePosition()
    {
        if (!smtc2_ || !timelineProps_ || !playback_.hasCurrentTrack() || playback_.currentTrack().durationMs <= 0)
            return;
        timelineProps_->put_Position(Abi::TimeSpan { playback_.positionMs() * 10000 });
        smtc2_->UpdateTimelineProperties(timelineProps_.Get());
    }

    // --- cover art ---------------------------------------------------

    void setCover(const QString& url)
    {
        if (url.isEmpty()) {
            pendingCoverUrl_.clear();
            pushThumbnail(QPixmap());
            return;
        }
        const QPixmap cover = covers_.pixmap(url, kCoverSize);
        if (!cover.isNull()) {
            pendingCoverUrl_.clear();
            pushThumbnail(cover);
            return;
        }
        // Still loading; onCoverReady() below picks it up once
        // CoverArtCache emits pixmapReady() for this url — the same wait
        // pattern main.cpp uses for the tray's track notification.
        pendingCoverUrl_ = url;
    }

    void onCoverReady(const QString& url)
    {
        if (pendingCoverUrl_.isEmpty() || url != pendingCoverUrl_)
            return;
        const QPixmap cover = covers_.pixmap(url, kCoverSize);
        if (cover.isNull())
            return; // another size of the same url landed; ours is still coming
        pendingCoverUrl_.clear();
        pushThumbnail(cover);
    }

    bool ensureStreamRefFactory()
    {
        if (streamRefFactory_)
            return true;
        HStringHolder className(QStringLiteral("Windows.Storage.Streams.RandomAccessStreamReference"));
        const HRESULT hr = RoGetActivationFactory(className.get(), Abi::IID_IRandomAccessStreamReferenceStatics,
            reinterpret_cast<void**>(streamRefFactory_.GetAddressOf()));
        if (FAILED(hr) || !streamRefFactory_) {
            qWarning() << "Smtc: RoGetActivationFactory(RandomAccessStreamReference) failed" << Qt::hex << hr;
            return false;
        }
        return true;
    }

    // Encodes `cover` to PNG in memory and hands it to SMTC via the classic
    // IStream -> WinRT IRandomAccessStream -> IRandomAccessStreamReference
    // chain — never an http(s) URL: the cover may have come through a
    // source's own proxy (Net::ProxyRouting) or be a local file, neither of
    // which Windows itself could fetch.
    void pushThumbnail(const QPixmap& cover)
    {
        if (!displayUpdater_)
            return;
        if (cover.isNull()) {
            displayUpdater_->put_Thumbnail(nullptr);
            displayUpdater_->Update();
            return;
        }

        QByteArray png;
        {
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            cover.save(&buffer, "PNG");
        }
        if (png.isEmpty())
            return;

        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(png.constData()), static_cast<UINT>(png.size())));
        if (!stream) {
            qWarning() << "Smtc: SHCreateMemStream failed";
            return;
        }

        ComPtr<IUnknown> randomAccessStream;
        HRESULT hr = CreateRandomAccessStreamOverStream(stream.Get(), BSOS_DEFAULT, Abi::IID_IRandomAccessStream,
            reinterpret_cast<void**>(randomAccessStream.GetAddressOf()));
        if (FAILED(hr) || !randomAccessStream) {
            qWarning() << "Smtc: CreateRandomAccessStreamOverStream failed" << Qt::hex << hr;
            return;
        }

        if (!ensureStreamRefFactory())
            return;
        ComPtr<IUnknown> streamRef;
        hr = streamRefFactory_->CreateFromStream(randomAccessStream.Get(), streamRef.GetAddressOf());
        if (FAILED(hr) || !streamRef) {
            qWarning() << "Smtc: CreateFromStream failed" << Qt::hex << hr;
            return;
        }

        displayUpdater_->put_Thumbnail(streamRef.Get());
        displayUpdater_->Update();
    }

    static constexpr QSize kCoverSize { 300, 300 };

    Playback::PlaybackController& playback_;
    Covers::CoverArtCache& covers_;
    SmtcService* owner_;

    bool classRegistered_ = false;
    HWND hwnd_ = nullptr;

    ComPtr<Abi::ISystemMediaTransportControls> smtc_;
    ComPtr<Abi::ISystemMediaTransportControls2> smtc2_;
    ComPtr<Abi::ISystemMediaTransportControlsDisplayUpdater> displayUpdater_;
    ComPtr<Abi::ISystemMediaTransportControlsTimelineProperties> timelineProps_;
    ComPtr<Abi::IRandomAccessStreamReferenceStatics> streamRefFactory_;

    Abi::EventRegistrationToken buttonToken_ { };
    Abi::EventRegistrationToken positionToken_ { };
    Abi::EventRegistrationToken shuffleToken_ { };
    Abi::EventRegistrationToken repeatToken_ { };
    bool buttonHandlerOk_ = false;
    bool positionHandlerOk_ = false;
    bool shuffleHandlerOk_ = false;
    bool repeatHandlerOk_ = false;

    QTimer* positionTimer_ = nullptr;
    QString pendingCoverUrl_;
};

SmtcService::SmtcService(Playback::PlaybackController& playback, Covers::CoverArtCache& covers, QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(playback, covers, this))
{
}

SmtcService::~SmtcService() = default;

} // namespace Integration
