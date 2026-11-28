// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.widget.ImageView;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

/**
 * The bar's space button (docs/ux.md §10: the space icon, left of the address), which is
 * Chromium's home button showing the space's emoji instead of the house: tapping it opens the
 * spaces sheet (android/patches/phone-shell.patch).
 */
@NullMarked
public final class SpaceButton {
    private SpaceButton() {}

    /** Shows {@code space} on {@code button}. */
    public static void show(View button, Space space) {
        button.setContentDescription(
                button.getContext().getString(R.string.lb_space_button, space.name));
        if (!(button instanceof ImageView)) return;
        ImageView image = (ImageView) button;
        // The theme's tint stays on the button: Chromium's toolbar reads it back when it
        // snapshots the bar. The emoji ignores it and keeps its own colours.
        image.setImageDrawable(new EmojiDrawable(space.icon));
        // The space's tint on the bar: a disc in its colour behind the emoji.
        GradientDrawable disc = new GradientDrawable();
        disc.setShape(GradientDrawable.OVAL);
        disc.setColor(withAlpha(SpaceStore.tint(space.hue), 0x55));
        image.setBackground(disc);
    }

    /** {@code argb} with its alpha replaced. */
    static int withAlpha(int argb, int alpha) {
        return (argb & 0x00ffffff) | (alpha << 24);
    }

    /**
     * A drawable of one emoji, centred and as big as the drawable's bounds allow, in the emoji's
     * own colours whatever the button's tint. Its own size is a toolbar icon's, 24 dp, so it looks
     * the same on every screen density.
     */
    static final class EmojiDrawable extends Drawable {
        static final int SIZE_DP = 24;

        private final String mEmoji;
        private final int mSize =
                Math.round(SIZE_DP * Resources.getSystem().getDisplayMetrics().density);
        private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        EmojiDrawable(String emoji) {
            mEmoji = emoji;
            mPaint.setTextAlign(Paint.Align.CENTER);
        }

        String emoji() {
            return mEmoji;
        }

        @Override
        public void draw(Canvas canvas) {
            Rect bounds = getBounds();
            if (bounds.isEmpty()) return;
            mPaint.setTextSize(bounds.height() * 0.8f);
            Paint.FontMetrics metrics = mPaint.getFontMetrics();
            float y = bounds.exactCenterY() - (metrics.ascent + metrics.descent) / 2;
            canvas.drawText(mEmoji, bounds.exactCenterX(), y, mPaint);
        }

        @Override
        public int getIntrinsicWidth() {
            return mSize;
        }

        @Override
        public int getIntrinsicHeight() {
            return mSize;
        }

        @Override
        public void setAlpha(int alpha) {
            mPaint.setAlpha(alpha);
        }

        @Override
        public void setColorFilter(@Nullable ColorFilter filter) {}

        @Override
        public void setTintList(@Nullable ColorStateList tint) {}

        @Override
        public int getOpacity() {
            return PixelFormat.TRANSLUCENT;
        }
    }
}
