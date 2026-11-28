// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.GradientDrawable;
import android.widget.ImageButton;

import androidx.core.widget.ImageViewCompat;
import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

/** The bar's space button shows the space's emoji in its own colours and says which space it is. */
@RunWith(BaseRobolectricTestRunner.class)
public class SpaceButtonTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SpaceStore.Natives mStore;

    private final Context mContext = ApplicationProvider.getApplicationContext();

    @Before
    public void setUp() {
        SpaceStore.setNativesForTesting(mStore);
        doReturn(0xffd99862).when(mStore).tint(30);
    }

    @Test
    public void showsTheSpacesEmojiAndKeepsTheThemesTint() {
        ImageButton button = new ImageButton(mContext);
        ImageViewCompat.setImageTintList(button, ColorStateList.valueOf(0xff000000));
        SpaceButton.show(button, new Space("a", "Work", "💼", 30, "Default"));
        assertTrue(button.getDrawable() instanceof SpaceButton.EmojiDrawable);
        assertEquals("💼", ((SpaceButton.EmojiDrawable) button.getDrawable()).emoji());
        // The theme's tint stays: Chromium's toolbar snapshot reads it and can't take null.
        assertEquals(0xff000000, ImageViewCompat.getImageTintList(button).getDefaultColor());
        assertEquals(
                mContext.getString(R.string.lb_space_button, "Work"),
                button.getContentDescription().toString());
        // The disc behind it is the space's tint, lighter.
        assertTrue(button.getBackground() instanceof GradientDrawable);
        assertEquals(
                0x55d99862,
                ((GradientDrawable) button.getBackground()).getColor().getDefaultColor());
    }

    @Test
    public void withAlphaReplacesOnlyTheAlpha() {
        assertEquals(0x22d99862, SpaceButton.withAlpha(0xffd99862, 0x22));
    }

    @Test
    public void theEmojiDrawsInAnyBounds() {
        SpaceButton.EmojiDrawable drawable = new SpaceButton.EmojiDrawable("🏠");
        Canvas canvas = new Canvas(Bitmap.createBitmap(48, 48, Bitmap.Config.ARGB_8888));
        drawable.draw(canvas); // empty bounds: nothing, no crash
        drawable.setBounds(0, 0, 48, 48);
        drawable.draw(canvas);
        assertTrue(drawable.getIntrinsicWidth() > 0);
    }

    @Test
    @Config(qualifiers = "xhdpi")
    public void theEmojiIsAToolbarIconsSizeOnAnyDensity() {
        SpaceButton.EmojiDrawable drawable = new SpaceButton.EmojiDrawable("🏠");
        // 24 dp at 2x.
        assertEquals(48, drawable.getIntrinsicWidth());
        assertEquals(48, drawable.getIntrinsicHeight());
    }

    @Test
    @Config(qualifiers = "mdpi")
    public void theEmojiIsAToolbarIconsSizeAtOneX() {
        SpaceButton.EmojiDrawable drawable = new SpaceButton.EmojiDrawable("🏠");
        assertEquals(24, drawable.getIntrinsicWidth());
        assertEquals(24, drawable.getIntrinsicHeight());
    }
}
