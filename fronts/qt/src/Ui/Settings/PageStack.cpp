#include "Settings/PageStack.h"

#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "OverlayScrollBar.h"
#include "ScrollEdgeFade.h"
#include "Settings/Page.h"
#include "SmoothScroller.h"
#include "Spacing.h"
#include "Tokens.h"
#include "Typography.h"

namespace Ui::Settings {

namespace {
constexpr int kColumnTopMargin = Theme::Spacing::space5;
constexpr int kSectionMaxWidth = 640;
} // namespace

PageStack::PageStack(QWidget* parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("settingsPages")); // see StyleSheet.cpp's settingsBlock()
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    column_ = new QWidget(this);
    columnLayout_ = new QVBoxLayout(column_);
    columnLayout_->setContentsMargins(
        Theme::Spacing::space6, kColumnTopMargin, Theme::Spacing::space6, Theme::Spacing::space6);
    columnLayout_->setSpacing(Theme::Spacing::space6);
    // Keeps a column shorter than the viewport top-aligned.
    columnLayout_->addStretch(1);
    setWidget(column_);
    // After setWidget(), which turns the widget's autoFillBackground on —
    // the column is transparent over the area's own QSS background.
    column_->setAutoFillBackground(false);

    scroller_ = SmoothScroller::attach(this);
    // Before OverlayScrollBar::attach — see ScrollEdgeFade's class doc.
    ScrollEdgeFade::attach(this, [] { return Theme::palette().surface0; });
    OverlayScrollBar::attach(this);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PageStack::onScrolled);
    connect(scroller_, &SmoothScroller::finished, this, &PageStack::onGlideFinished);
}

void PageStack::addPage(Page* page)
{
    Section section;
    section.page = page;
    section.frame = new QWidget(column_);
    section.frame->setMaximumWidth(kSectionMaxWidth);
    section.frameLayout = new QVBoxLayout(section.frame);
    section.frameLayout->setContentsMargins(0, 0, 0, 0);
    section.frameLayout->setSpacing(Theme::Spacing::space3);

    auto* title = new QLabel(page->title(), section.frame);
    title->setFont(Theme::font(Theme::TextStyle::Title));
    section.frameLayout->addWidget(title);

    section.placeholder = new QWidget(section.frame);
    section.placeholder->setFixedHeight(page->estimatedHeight());
    section.frameLayout->addWidget(section.placeholder);

    // Before the trailing stretch.
    columnLayout_->insertWidget(columnLayout_->count() - 1, section.frame);
    sections_.push_back(section);
}

void PageStack::materialize(Section& section)
{
    QWidget* body = section.page->createWidget(section.frame);
    section.frameLayout->replaceWidget(section.placeholder, body);
    delete section.placeholder;
    section.placeholder = nullptr;
    // A child added to an already-visible parent is only shown by a queued
    // call, and a layout counts a not-yet-shown widget as empty — its real
    // height wouldn't be there for syncGeometry() to pick up.
    body->show();
    qDebug() << "Settings: materialized page" << section.page->id();
}

void PageStack::syncGeometry()
{
    columnLayout_->activate();
    // Re-setting widgetResizable runs QScrollArea's own "size the widget to
    // the viewport and its minimum size, update the scrollbar range" pass
    // synchronously — otherwise that only happens on a posted event.
    setWidgetResizable(true);
}

void PageStack::materializeNearViewport()
{
    // pendingPage_: the first real position is still to come — building
    // what happens to be at the top first would be wasted.
    if (jumping_ || adjusting_ || pendingPage_ >= 0 || !isVisible())
        return;
    QScrollBar* bar = verticalScrollBar();
    const int viewportHeight = viewport()->height();

    // Every pass can shrink or grow the column, pulling more placeholders
    // into range; bounded by the section count.
    for (;;) {
        // Also before the first pass: called from a resize or scroll, the
        // sections may not have been laid out for the current size yet.
        syncGeometry();
        const int top = bar->value();
        const int windowTop = top - viewportHeight;
        const int windowBottom = top + 2 * viewportHeight;

        // The first section still (partly) on screen, and how far into it
        // the viewport starts: kept fixed across the materialization.
        const Section* anchor = nullptr;
        int anchorOffset = 0;
        for (const Section& section : sections_) {
            if (section.frame->geometry().bottom() >= top) {
                anchor = &section;
                anchorOffset = top - section.frame->y();
                break;
            }
        }

        bool changed = false;
        for (Section& section : sections_) {
            const QRect geometry = section.frame->geometry();
            if (section.placeholder && geometry.bottom() >= windowTop && geometry.top() <= windowBottom) {
                materialize(section);
                changed = true;
            }
        }
        if (!changed)
            return;

        syncGeometry();
        if (anchor) {
            const int delta = anchor->frame->y() + anchorOffset - top;
            if (delta != 0) {
                adjusting_ = true;
                scroller_->shift(delta);
                if (pinned_ >= 0)
                    landedValue_ = bar->value();
                adjusting_ = false;
            }
        }
    }
}

int PageStack::scrollTargetFor(int index) const
{
    // The heading lands where the first one sits with the column at rest.
    return qMax(0, sections_[index].frame->y() - kColumnTopMargin);
}

void PageStack::scrollToPage(int index, bool animated)
{
    if (index < 0 || index >= int(sections_.size()))
        return;
    if (!isVisible()) {
        pendingPage_ = index;
        return;
    }
    if (jumping_ && pinned_ == index)
        return; // already on its way there

    // Build the destination and whatever will fill the viewport below it
    // up front, so the landing position (and, near the end of the column,
    // the scrollbar's maximum it's clamped to) is final before the glide
    // starts. Sections above it materialize after landing, anchored.
    const int viewportHeight = viewport()->height();
    int filled = 0;
    for (int i = index; i < int(sections_.size()) && filled < 2 * viewportHeight; ++i) {
        if (sections_[i].placeholder) {
            materialize(sections_[i]);
            syncGeometry();
        }
        filled += sections_[i].frame->height() + columnLayout_->spacing();
    }

    jumpTarget_ = qMin(scrollTargetFor(index), verticalScrollBar()->maximum());
    pinned_ = index;
    setCurrentPage(index);

    if (!animated || verticalScrollBar()->value() == jumpTarget_) {
        adjusting_ = true;
        verticalScrollBar()->setValue(jumpTarget_);
        adjusting_ = false;
        landedValue_ = jumpTarget_;
        materializeNearViewport();
        return;
    }
    jumping_ = true;
    scroller_->scrollTo(jumpTarget_);
}

void PageStack::onGlideFinished()
{
    if (!jumping_)
        return;
    jumping_ = false;
    landedValue_ = verticalScrollBar()->value();
    // Wheeled somewhere else mid-jump: that's the user's own position now.
    if (landedValue_ != jumpTarget_)
        pinned_ = -1;
    materializeNearViewport();
    updateCurrentPage();
}

void PageStack::onScrolled()
{
    if (adjusting_)
        return;
    if (!jumping_ && pinned_ >= 0 && verticalScrollBar()->value() != landedValue_)
        pinned_ = -1;
    materializeNearViewport();
    updateCurrentPage();
}

void PageStack::updateCurrentPage()
{
    if (sections_.empty())
        return;
    if (pinned_ >= 0) {
        setCurrentPage(pinned_);
        return;
    }
    const QScrollBar* bar = verticalScrollBar();
    // At the very end, the last section is current even if its heading
    // never made it to the top.
    if (bar->maximum() > 0 && bar->value() >= bar->maximum()) {
        setCurrentPage(int(sections_.size()) - 1);
        return;
    }
    // Otherwise: the last section whose heading has reached the top.
    const int line = bar->value() + kColumnTopMargin + Theme::Spacing::space5;
    int index = 0;
    for (int i = 0; i < int(sections_.size()); ++i) {
        if (sections_[i].frame->y() <= line)
            index = i;
    }
    setCurrentPage(index);
}

void PageStack::setCurrentPage(int index)
{
    if (index == current_)
        return;
    current_ = index;
    emit currentPageChanged(index);
}

void PageStack::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    materializeNearViewport();
}

void PageStack::showEvent(QShowEvent* event)
{
    QScrollArea::showEvent(event);
    // Queued: the dialog's layout gives this area its real size only after
    // the show event.
    QTimer::singleShot(0, this, [this]() {
        syncGeometry();
        if (pendingPage_ >= 0) {
            const int page = pendingPage_;
            pendingPage_ = -1;
            scrollToPage(page, false);
        } else {
            materializeNearViewport();
            updateCurrentPage();
        }
    });
}

} // namespace Ui::Settings
