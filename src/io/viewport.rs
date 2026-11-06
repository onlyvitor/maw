// The window onto the buffer: which logical rows are on screen and how
// wide a row may be. A logical line always occupies exactly one screen
// row; the renderer clips it to the terminal width. This module is a
// pure model with no I/O and no escape codes.

use std::ops::Range;

/// Terminal geometry in cells.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Size {
    pub rows: usize,
    pub columns: usize,
}

impl Size {
    /// 80x24, the conventional fallback for terminals that report
    /// nothing.
    pub const FALLBACK: Size = Size {
        rows: 24,
        columns: 80,
    };

    pub fn new(rows: usize, columns: usize) -> Size {
        Size {
            rows: rows.max(1),
            columns: columns.max(1),
        }
    }

    /// How many bytes of a row fit on screen. The last cell is left
    /// unused so a full row never parks the terminal in the
    /// wrap-pending state.
    pub fn content_width(self) -> usize {
        self.columns.saturating_sub(1).max(1)
    }
}

/// The visible slice of the buffer. `top_row` is the first buffer row
/// drawn at the top of the screen.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Viewport {
    top_row: usize,
    size: Size,
}

impl Viewport {
    pub fn new(size: Size) -> Viewport {
        Viewport { top_row: 0, size }
    }

    pub fn size(&self) -> Size {
        self.size
    }

    pub fn top_row(&self) -> usize {
        self.top_row
    }

    /// Adopt a new geometry. True means the screen has to be repainted
    /// from scratch.
    pub fn resize(&mut self, size: Size) -> bool {
        if self.size == size {
            return false;
        }

        self.size = size;
        true
    }

    /// Scroll the least amount that puts `cursor_row` on screen. True
    /// means the viewport moved, so the screen has to be repainted.
    pub fn ensure_visible(&mut self, cursor_row: usize) -> bool {
        if cursor_row < self.top_row {
            self.top_row = cursor_row;
            true
        } else if cursor_row >= self.top_row + self.size.rows {
            self.top_row = cursor_row + 1 - self.size.rows;
            true
        } else {
            false
        }
    }

    /// Screen row (0-based) where a buffer row lands. Only meaningful
    /// for rows inside `visible_rows`.
    pub fn screen_row(&self, row: usize) -> usize {
        row - self.top_row
    }

    /// Buffer rows currently on screen.
    pub fn visible_rows(&self) -> Range<usize> {
        self.top_row..self.top_row + self.size.rows
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn viewport(rows: usize, columns: usize) -> Viewport {
        Viewport::new(Size::new(rows, columns))
    }

    #[test]
    fn a_new_viewport_starts_at_the_first_row() {
        let vp = viewport(24, 80);

        assert_eq!(vp.top_row(), 0);
        assert_eq!(vp.visible_rows(), 0..24);
    }

    #[test]
    fn ensure_visible_does_nothing_inside_the_window() {
        let mut vp = viewport(10, 80);

        assert!(!vp.ensure_visible(0));
        assert!(!vp.ensure_visible(9));
        assert_eq!(vp.top_row(), 0);

        vp.ensure_visible(20);
        assert!(!vp.ensure_visible(11));
        assert!(!vp.ensure_visible(20));
        assert_eq!(vp.top_row(), 11);
    }

    #[test]
    fn ensure_visible_scrolls_down_the_least_amount() {
        let mut vp = viewport(10, 80);

        /* Rows 0..9 are visible; row 10 forces one row of scroll. */
        assert!(vp.ensure_visible(10));
        assert_eq!(vp.top_row(), 1);
        assert_eq!(vp.visible_rows(), 1..11);

        assert!(vp.ensure_visible(30));
        assert_eq!(vp.top_row(), 21);
        assert_eq!(vp.visible_rows(), 21..31);
    }

    #[test]
    fn ensure_visible_scrolls_up_to_the_cursor() {
        let mut vp = viewport(10, 80);
        vp.ensure_visible(30);

        assert!(vp.ensure_visible(5));
        assert_eq!(vp.top_row(), 5);
        assert_eq!(vp.visible_rows(), 5..15);
    }

    #[test]
    fn screen_row_is_relative_to_the_top() {
        let mut vp = viewport(10, 80);
        vp.ensure_visible(30);

        assert_eq!(vp.screen_row(21), 0);
        assert_eq!(vp.screen_row(30), 9);
    }

    #[test]
    fn resize_only_reports_real_changes() {
        let mut vp = viewport(24, 80);

        assert!(!vp.resize(Size::new(24, 80)));
        assert!(vp.resize(Size::new(30, 100)));
        assert_eq!(vp.size(), Size::new(30, 100));
        assert!(!vp.resize(Size::new(30, 100)));
    }

    #[test]
    fn zero_sized_terminals_are_clamped_to_one_cell() {
        let size = Size::new(0, 0);

        assert_eq!(size, Size::new(1, 1));
        assert_eq!(size.content_width(), 1);
    }

    #[test]
    fn content_width_leaves_the_last_cell_free() {
        assert_eq!(Size::new(24, 80).content_width(), 79);
        assert_eq!(Size::new(24, 2).content_width(), 1);
    }
}
