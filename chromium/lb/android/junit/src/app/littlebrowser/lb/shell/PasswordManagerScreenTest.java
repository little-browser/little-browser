// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.content.Intent;
import android.content.pm.PackageInfo;
import android.provider.Settings;
import android.view.ViewGroup;

import androidx.activity.ComponentActivity;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;

import org.chromium.base.ContextUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.components.prefs.PrefService;

import app.littlebrowser.lb.password.PasswordManagers;

/**
 * First run's password-manager question: asked once, a pick goes to the store when the app is
 * missing and to Android's autofill picker once it is there, and "Not now" just closes it.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class PasswordManagerScreenTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private PasswordManagers.Natives mNatives;
    @Mock private PrefService mPrefs;

    private ComponentActivity mActivity;

    @Before
    public void setUp() {
        // androidx's activity, as Chromium's are, so Back goes through its dispatcher.
        mActivity = Robolectric.buildActivity(ComponentActivity.class).setup().get();
        ContextUtils.getAppSharedPreferences()
                .edit()
                .remove(PasswordManagerScreen.PENDING_PREF)
                .commit();
        PasswordManagers.setNativesForTesting(mNatives);
        doReturn(new String[] {"A", "About A", "a.app", "B", "About B", "b.app"})
                .when(mNatives)
                .list();
    }

    private void install(String androidPackage) {
        PackageInfo info = new PackageInfo();
        info.packageName = androidPackage;
        Shadows.shadowOf(mActivity.getPackageManager()).installPackage(info);
    }

    private Intent nextStarted() {
        return Shadows.shadowOf(mActivity).getNextStartedActivity();
    }

    @Test
    public void aPickGoesToTheStoreUntilTheAppIsInstalled() {
        assertEquals(PasswordManagerScreen.Next.STORE, PasswordManagerScreen.afterPick(false));
        assertEquals(
                PasswordManagerScreen.Next.AUTOFILL_PICKER, PasswordManagerScreen.afterPick(true));
    }

    @Test
    public void onlyTheOfferedAppsThatAreInstalledCount() {
        install("b.app");
        install("other.app");
        assertEquals(
                java.util.List.of("b.app"),
                PasswordManagerScreen.installed(
                        mActivity.getPackageManager(), PasswordManagers.list()));
    }

    @Test
    public void askedBeforeItIsSkipped() {
        doReturn(true).when(mPrefs).getBoolean(PasswordManagerScreen.SHOWN_PREF);
        doReturn(PasswordManagers.Step.SKIP)
                .when(mNatives)
                .nextStep(anyBoolean(), anyBoolean(), any());
        assertNull(PasswordManagerScreen.maybeShow(mActivity, mPrefs));
        verify(mPrefs, never()).setBoolean(PasswordManagerScreen.SHOWN_PREF, true);
    }

    @Test
    public void oneInstalledSettlesItForGood() {
        doReturn(PasswordManagers.Step.SETTLED)
                .when(mNatives)
                .nextStep(anyBoolean(), anyBoolean(), any());
        assertNull(PasswordManagerScreen.maybeShow(mActivity, mPrefs));
        verify(mPrefs).setBoolean(PasswordManagerScreen.SHOWN_PREF, true);
        // The installed one fills forms: Chromium hands them to Android's autofill service.
        verify(mPrefs).setBoolean(PasswordManagerScreen.PLATFORM_AUTOFILL_PREF, true);
    }

    @Test
    public void offeredItCoversTheWindowAndIsntAskedAgain() {
        doReturn(PasswordManagers.Step.OFFER)
                .when(mNatives)
                .nextStep(anyBoolean(), anyBoolean(), any());
        PasswordManagerScreen screen = PasswordManagerScreen.maybeShow(mActivity, mPrefs);
        assertNotNull(screen);
        assertTrue(screen.isShown());
        ViewGroup content = mActivity.findViewById(android.R.id.content);
        assertSame(content, screen.viewForTesting().getParent());
        verify(mPrefs).setBoolean(PasswordManagerScreen.SHOWN_PREF, true);
        // "Not now" changes nothing else.
        screen.dismiss();
        assertNull(screen.viewForTesting().getParent());
        verify(mPrefs, never()).setBoolean(PasswordManagerScreen.PLATFORM_AUTOFILL_PREF, true);
    }

    @Test
    public void anInstalledPickOpensTheAutofillPicker() {
        install("a.app");
        PasswordManagerScreen screen =
                new PasswordManagerScreen(mActivity, mPrefs, PasswordManagers.list());
        screen.show();
        screen.pick("a.app");
        verify(mPrefs).setBoolean(PasswordManagerScreen.PLATFORM_AUTOFILL_PREF, true);
        Intent picker = nextStarted();
        assertEquals(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE, picker.getAction());
        assertEquals(PasswordManagerScreen.ANY_SERVICE, picker.getDataString());
        assertFalse(screen.isShown());
    }

    @Test
    public void aMissingPickOpensTheStoreThenThePickerOnceInstalled() {
        PasswordManagerScreen screen =
                new PasswordManagerScreen(mActivity, mPrefs, PasswordManagers.list());
        screen.show();
        screen.pick("b.app");
        Intent store = nextStarted();
        assertEquals(Intent.ACTION_VIEW, store.getAction());
        assertTrue(store.getDataString(), store.getDataString().endsWith("id=b.app"));
        assertTrue(screen.isShown());
        // Back without installing it: nothing more.
        screen.viewForTesting().onWindowFocusChanged(true);
        assertNull(nextStarted());
        // Back with it installed: the picker, and the question is done.
        install("b.app");
        screen.viewForTesting().onWindowFocusChanged(true);
        assertEquals(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE, nextStarted().getAction());
        assertFalse(screen.isShown());
    }

    @Test
    public void backClosesIt() {
        PasswordManagerScreen screen =
                new PasswordManagerScreen(mActivity, mPrefs, PasswordManagers.list());
        screen.show();
        assertTrue(mActivity.getOnBackPressedDispatcher().hasEnabledCallbacks());
        mActivity.getOnBackPressedDispatcher().onBackPressed();
        assertFalse(screen.isShown());
        assertNull(screen.viewForTesting().getParent());
        assertFalse(mActivity.getOnBackPressedDispatcher().hasEnabledCallbacks());
    }

    private void askedBefore() {
        doReturn(true).when(mPrefs).getBoolean(PasswordManagerScreen.SHOWN_PREF);
        doReturn(PasswordManagers.Step.SKIP)
                .when(mNatives)
                .nextStep(anyBoolean(), anyBoolean(), any());
    }

    @Test
    public void aPickTheWindowDidntOutliveOpensThePickerNextTime() {
        PasswordManagerScreen screen =
                new PasswordManagerScreen(mActivity, mPrefs, PasswordManagers.list());
        screen.show();
        screen.pick("b.app");
        assertEquals(Intent.ACTION_VIEW, nextStarted().getAction());
        // The browser is gone before it is back from the store; the app was installed meanwhile.
        install("b.app");
        askedBefore();
        assertNull(PasswordManagerScreen.maybeShow(mActivity, mPrefs));
        assertEquals(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE, nextStarted().getAction());
        // Only once.
        assertNull(PasswordManagerScreen.maybeShow(mActivity, mPrefs));
        assertNull(nextStarted());
    }

    @Test
    public void aPickTheWindowDidntOutliveAsksAgainIfTheAppIsntThere() {
        PasswordManagerScreen screen =
                new PasswordManagerScreen(mActivity, mPrefs, PasswordManagers.list());
        screen.show();
        screen.pick("b.app");
        nextStarted();
        askedBefore();
        PasswordManagerScreen again = PasswordManagerScreen.maybeShow(mActivity, mPrefs);
        assertNotNull(again);
        assertTrue(again.isShown());
        assertNull(nextStarted());
    }

    @Test
    public void notNowAfterTheStoreDropsThePick() {
        PasswordManagerScreen screen =
                new PasswordManagerScreen(mActivity, mPrefs, PasswordManagers.list());
        screen.show();
        screen.pick("b.app");
        nextStarted();
        screen.dismiss();
        install("b.app");
        askedBefore();
        assertNull(PasswordManagerScreen.maybeShow(mActivity, mPrefs));
        assertNull(nextStarted());
    }
}
