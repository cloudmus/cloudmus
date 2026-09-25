#pragma once

#include <QScrollArea>

#include <vector>

class QVBoxLayout;

namespace Ui {
class SmoothScroller;
}

namespace Ui::Settings {

class Page;
class SectionCard;

// The Settings dialog's right-hand side: every page as one section of a
// single vertically scrolling column, so settings can be browsed by just
// scrolling, while the sidebar jumps straight to a section.
//
// Sections are built lazily, the way mobile infinite lists create
// delegates: until a section comes within one viewport height of the
// visible area, its body is a fixed-height placeholder sized by
// Page::estimatedHeight(), so opening the dialog costs only what's on
// screen. Once built a section is kept — it may hold unapplied edits. When
// a section above the visible area materializes and turns out taller or
// shorter than its estimate, the scroll position is shifted by the
// difference (see SmoothScroller::shift()), so what's on screen stays put.
class PageStack : public QScrollArea {
    Q_OBJECT

public:
    explicit PageStack(QWidget* parent = nullptr);

    void addPage(Page* page);

    // Glides the column so section `index`'s heading is at the top (or as
    // close as the column's end allows), then — if animated — briefly
    // flashes its card's border, so the eye finds the group the sidebar
    // pointed at even when it didn't reach the top. Keeps that section current
    // until the user scrolls on their own — a short last section may never
    // reach the top, and the "which heading is at the top" rule would
    // otherwise report a different one than was just clicked.
    void scrollToPage(int index, bool animated = true);

    int currentPage() const { return current_; }

signals:
    void currentPageChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Section {
        Page* page = nullptr;
        // The heading plus the card below it; positions and heights are
        // the frame's.
        QWidget* frame = nullptr;
        SectionCard* card = nullptr;
        QVBoxLayout* cardLayout = nullptr;
        // Non-null until the page's own widget replaces it.
        QWidget* placeholder = nullptr;
    };

    void materialize(Section& section);
    void materializeNearViewport();
    // Makes the column's size, its sections' positions and the scrollbar's
    // range reflect sections just materialized, right now instead of on
    // the next posted LayoutRequest — the anchoring and jump math read them
    // immediately.
    void syncGeometry();
    int scrollTargetFor(int index) const;
    void onScrolled();
    void onGlideFinished();
    void flash(int index);
    void updateCurrentPage();
    void setCurrentPage(int index);

    QWidget* column_ = nullptr;
    QVBoxLayout* columnLayout_ = nullptr;
    SmoothScroller* scroller_ = nullptr;
    std::vector<Section> sections_;
    int current_ = -1;
    // A sidebar jump in flight: sections it passes over keep their
    // placeholders until it lands, so nothing between here and there
    // changes height and moves the destination mid-glide.
    bool jumping_ = false;
    int jumpTarget_ = 0;
    // The section a jump landed on, held as current until the user moves
    // away from landedValue_; -1 when not pinned.
    int pinned_ = -1;
    int landedValue_ = 0;
    // Set while this class moves the scrollbar itself (anchoring), so its
    // own valueChanged doesn't count as the user scrolling.
    bool adjusting_ = false;
    // The section flash() last pointed out; -1 before the first.
    int flashing_ = -1;
    // scrollToPage() before the first show: geometry isn't real yet.
    int pendingPage_ = -1;
};

} // namespace Ui::Settings
