#pragma once

// Windows.Media.SystemMediaTransportControls — the WinRT ABI surface,
// declared by hand because mingw-w64's own windows.media.h only has the
// pre-WinRT IMediaControl (DirectShow), not the SMTC types, and the mingw
// toolchain has no C++/WinRT. The interfaces, vtable order and enum values
// below are transcribed from the real Windows metadata, via the
// microsoft/windows-rs generated bindings (MIT-licensed, generated
// directly from Windows.Media.winmd and Windows.Storage.Streams.winmd):
//   crates/libs/windows/src/Windows/Media/mod.rs
//   crates/libs/windows/src/Windows/Storage/Streams/mod.rs
// (commit a38689a8b29520db84e3d398e4389d48f18e6346), cross-checked against
// https://learn.microsoft.com/uwp/api/windows.media.systemmediatransportcontrols
//
// The four TypedEventHandler<SystemMediaTransportControls, ...> delegate
// IIDs are WinRT's own parameterized-interface hash: SHA-1 over an ASCII
// "pinterface({generic-guid};{sender-signature};{args-signature})" string,
// seeded with the namespace GUID 11f47ad5-7b73-42c0-abae-878b1e16adee (see
// GUID::from_signature in windows-rs' windows_core crate, crates/libs/core/
// src/guid.rs). Computed with a throwaway script implementing that same
// algorithm and cross-checked against windows-rs' own test vectors in
// crates/tests/winrt/old/tests/generic_guids.rs (reproduced one of its
// assertions byte-for-byte before trusting it for our own signatures).

#include <inspectable.h>
#include <unknwn.h>

namespace Integration::Smtc::Abi {

// --- Interface, delegate and activation-factory ids -----------------------
// (kept in one block so every literal below has exactly one home; the
// interface declarations further down just reference these by name).

constexpr GUID IID_ISystemMediaTransportControls
    = { 0x99fa3ff4, 0x1742, 0x42a6, { 0x90, 0x2e, 0x08, 0x7d, 0x41, 0xf9, 0x65, 0xec } };
constexpr GUID IID_ISystemMediaTransportControls2
    = { 0xea98d2f6, 0x7f3c, 0x4af2, { 0xa5, 0x86, 0x72, 0x88, 0x98, 0x08, 0xef, 0xb1 } };
constexpr GUID IID_ISystemMediaTransportControlsButtonPressedEventArgs
    = { 0xb7f47116, 0xa56f, 0x4dc8, { 0x9e, 0x11, 0x92, 0x03, 0x1f, 0x4a, 0x87, 0xc2 } };
constexpr GUID IID_ISystemMediaTransportControlsDisplayUpdater
    = { 0x8abbc53e, 0xfa55, 0x4ecf, { 0xad, 0x8e, 0xc9, 0x84, 0xe5, 0xdd, 0x15, 0x50 } };
constexpr GUID IID_IMusicDisplayProperties
    = { 0x6bbf0c59, 0xd0a0, 0x4d26, { 0x92, 0xa0, 0xf9, 0x78, 0xe1, 0xd1, 0x8e, 0x7b } };
constexpr GUID IID_IMusicDisplayProperties2
    = { 0x00368462, 0x97d3, 0x44b9, { 0xb0, 0x0f, 0x00, 0x8a, 0xfc, 0xef, 0xaf, 0x18 } };
constexpr GUID IID_ISystemMediaTransportControlsTimelineProperties
    = { 0x5125316a, 0xc3a2, 0x475b, { 0x85, 0x07, 0x93, 0x53, 0x4d, 0xc8, 0x8f, 0x15 } };
constexpr GUID IID_IPlaybackPositionChangeRequestedEventArgs
    = { 0xb4493f88, 0xeb28, 0x4961, { 0x9c, 0x14, 0x33, 0x5e, 0x44, 0xf3, 0xe1, 0x25 } };
constexpr GUID IID_IShuffleEnabledChangeRequestedEventArgs
    = { 0x49b593fe, 0x4fd0, 0x4666, { 0xa3, 0x14, 0xc0, 0xe0, 0x19, 0x40, 0xd3, 0x02 } };
constexpr GUID IID_IAutoRepeatModeChangeRequestedEventArgs
    = { 0xea137efa, 0xd852, 0x438e, { 0x88, 0x2b, 0xc9, 0x90, 0x10, 0x9a, 0x78, 0xf4 } };
constexpr GUID IID_IRandomAccessStreamReferenceStatics
    = { 0x857309dc, 0x3fbf, 0x4e7d, { 0x98, 0x6f, 0xef, 0x3b, 0x1a, 0x07, 0xa9, 0x64 } };
// Windows.Storage.Streams.IRandomAccessStream — never called through (we
// only ever pass the pointer on to CreateFromStream), so it isn't declared
// below; only its id, to ask CreateRandomAccessStreamOverStream() for it.
constexpr GUID IID_IRandomAccessStream
    = { 0x905a0fe1, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };

// TypedEventHandler<SystemMediaTransportControls, XxxEventArgs> — see the
// file header for how these are derived.
constexpr GUID IID_ButtonPressedHandler
    = { 0x0557e996, 0x7b23, 0x5bae, { 0xaa, 0x81, 0xea, 0x0d, 0x67, 0x11, 0x43, 0xa4 } };
constexpr GUID IID_PlaybackPositionChangeRequestedHandler
    = { 0x44e34f15, 0xbdc0, 0x50a7, { 0xac, 0xe4, 0x39, 0xe9, 0x1f, 0xb7, 0x53, 0xf1 } };
constexpr GUID IID_ShuffleEnabledChangeRequestedHandler
    = { 0x17ecea80, 0x27e4, 0x5dae, { 0xab, 0xb4, 0xc8, 0x58, 0xad, 0x1c, 0x53, 0x07 } };
constexpr GUID IID_AutoRepeatModeChangeRequestedHandler
    = { 0xa6214bde, 0x02d5, 0x55b3, { 0xab, 0x0d, 0xc6, 0x03, 0x1b, 0xe7, 0x0d, 0xa1 } };

// Runtime class names, for RoGetActivationFactory()/RoActivateInstance().
inline constexpr wchar_t kClassSystemMediaTransportControls[] = L"Windows.Media.SystemMediaTransportControls";
inline constexpr wchar_t kClassSystemMediaTransportControlsTimelineProperties[]
    = L"Windows.Media.SystemMediaTransportControlsTimelineProperties";
inline constexpr wchar_t kClassRandomAccessStreamReference[] = L"Windows.Storage.Streams.RandomAccessStreamReference";

// --- Plain WinRT value types ------------------------------------------------

// Windows.Foundation.TimeSpan: 100ns ticks.
struct TimeSpan {
    INT64 Duration;
};

struct EventRegistrationToken {
    INT64 value;
};

enum class MediaPlaybackStatus : int {
    Closed = 0,
    Changing = 1,
    Stopped = 2,
    Playing = 3,
    Paused = 4,
};

enum class MediaPlaybackType : int {
    Unknown = 0,
    Music = 1,
    Video = 2,
    Image = 3,
};

enum class MediaPlaybackAutoRepeatMode : int {
    None = 0,
    Track = 1,
    List = 2,
};

enum class SystemMediaTransportControlsButton : int {
    Play = 0,
    Pause = 1,
    Stop = 2,
    Record = 3,
    FastForward = 4,
    Rewind = 5,
    Next = 6,
    Previous = 7,
    ChannelUp = 8,
    ChannelDown = 9,
};

// --- Interfaces --------------------------------------------------------
// Pure-virtual COM interfaces, IInspectable-derived like every WinRT type;
// vtable slot order matches the metadata exactly (methods we never call —
// SoundLevel, VideoProperties, CopyFromFileAsync, ... — are still declared,
// with a generic IUnknown*/int* payload, purely to keep every later slot at
// the right offset).

struct IMusicDisplayProperties;
struct ISystemMediaTransportControlsDisplayUpdater;

// IID_ISystemMediaTransportControls
struct ISystemMediaTransportControls : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(MediaPlaybackStatus* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(MediaPlaybackStatus value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(ISystemMediaTransportControlsDisplayUpdater** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(int* value) = 0; // SoundLevel enum, unused
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelUpEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelDownEnabled(boolean value) = 0;
    // handler: IUnknown* standing in for TypedEventHandler<SystemMediaTransportControls,
    // SystemMediaTransportControlsButtonPressedEventArgs>* (IID_ButtonPressedHandler).
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PropertyChanged(IUnknown* handler, EventRegistrationToken* token)
        = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE remove_PropertyChanged(EventRegistrationToken token) = 0; // unused
};

// IID_ISystemMediaTransportControls2 — a second interface on the same
// object, reached with QueryInterface(IID_ISystemMediaTransportControls2).
struct ISystemMediaTransportControls2 : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_AutoRepeatMode(MediaPlaybackAutoRepeatMode* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AutoRepeatMode(MediaPlaybackAutoRepeatMode value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShuffleEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShuffleEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(double* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackRate(double value) = 0; // unused
    // timelineProperties: ISystemMediaTransportControlsTimelineProperties*.
    virtual HRESULT STDMETHODCALLTYPE UpdateTimelineProperties(IUnknown* timelineProperties) = 0;
    // handler: TypedEventHandler<SMTC, PlaybackPositionChangeRequestedEventArgs>*
    // (IID_PlaybackPositionChangeRequestedHandler).
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackPositionChangeRequested(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackPositionChangeRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackRateChangeRequested(IUnknown* handler, EventRegistrationToken* token)
        = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackRateChangeRequested(EventRegistrationToken token) = 0; // unused
    // handler: TypedEventHandler<SMTC, ShuffleEnabledChangeRequestedEventArgs>*
    // (IID_ShuffleEnabledChangeRequestedHandler).
    virtual HRESULT STDMETHODCALLTYPE add_ShuffleEnabledChangeRequested(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ShuffleEnabledChangeRequested(EventRegistrationToken token) = 0;
    // handler: TypedEventHandler<SMTC, AutoRepeatModeChangeRequestedEventArgs>*
    // (IID_AutoRepeatModeChangeRequestedHandler).
    virtual HRESULT STDMETHODCALLTYPE add_AutoRepeatModeChangeRequested(
        IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AutoRepeatModeChangeRequested(EventRegistrationToken token) = 0;
};

// IID_ISystemMediaTransportControlsButtonPressedEventArgs
struct ISystemMediaTransportControlsButtonPressedEventArgs : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Button(SystemMediaTransportControlsButton* value) = 0;
};

// IID_IMusicDisplayProperties
struct IMusicDisplayProperties : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumArtist(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Artist(HSTRING value) = 0;
};

// IID_IMusicDisplayProperties2 — reached with QueryInterface on the same
// object IMusicDisplayProperties::get_MusicProperties() returned.
struct IMusicDisplayProperties2 : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumTitle(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(UINT32* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_TrackNumber(UINT32 value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE get_Genres(IUnknown** value) = 0; // IVector<HSTRING>, unused
};

// IID_ISystemMediaTransportControlsDisplayUpdater
struct ISystemMediaTransportControlsDisplayUpdater : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Type(MediaPlaybackType* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Type(MediaPlaybackType value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IUnknown** value) = 0; // IRandomAccessStreamReference*, unused
    // value: IRandomAccessStreamReference* (from IRandomAccessStreamReferenceStatics::CreateFromStream()).
    virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(IUnknown* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(IMusicDisplayProperties** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(IUnknown** value) = 0; // IVideoDisplayProperties*, unused
    virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(IUnknown** value) = 0; // IImageDisplayProperties*, unused
    virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(MediaPlaybackType type, IUnknown* file, IUnknown** operation)
        = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE ClearAll() = 0;
    virtual HRESULT STDMETHODCALLTYPE Update() = 0;
};

// IID_ISystemMediaTransportControlsTimelineProperties — implemented by the
// activatable class SystemMediaTransportControlsTimelineProperties
// (kClassSystemMediaTransportControlsTimelineProperties).
struct ISystemMediaTransportControlsTimelineProperties : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(TimeSpan* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_StartTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(TimeSpan* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_EndTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(TimeSpan* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_MinSeekTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(TimeSpan* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_MaxSeekTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(TimeSpan* value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE put_Position(TimeSpan value) = 0;
};

// IID_IPlaybackPositionChangeRequestedEventArgs
struct IPlaybackPositionChangeRequestedEventArgs : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_RequestedPlaybackPosition(TimeSpan* value) = 0;
};

// IID_IShuffleEnabledChangeRequestedEventArgs
struct IShuffleEnabledChangeRequestedEventArgs : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_RequestedShuffleEnabled(boolean* value) = 0;
};

// IID_IAutoRepeatModeChangeRequestedEventArgs
struct IAutoRepeatModeChangeRequestedEventArgs : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_RequestedAutoRepeatMode(MediaPlaybackAutoRepeatMode* value) = 0;
};

// IID_IRandomAccessStreamReferenceStatics — the activation factory for
// kClassRandomAccessStreamReference (RoGetActivationFactory).
struct IRandomAccessStreamReferenceStatics : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE CreateFromFile(IUnknown* file, IUnknown** value) = 0; // unused
    virtual HRESULT STDMETHODCALLTYPE CreateFromUri(IUnknown* uri, IUnknown** value) = 0; // unused
    // stream: IRandomAccessStream* (IID_IRandomAccessStream); value: IRandomAccessStreamReference**.
    virtual HRESULT STDMETHODCALLTYPE CreateFromStream(IUnknown* stream, IUnknown** value) = 0;
};

} // namespace Integration::Smtc::Abi
