package io.github.sonsegajp.anthology;

import android.app.Activity;
import android.content.pm.ActivityInfo;

/** Which way up the app runs. A phone: landscape, either way up. A large
 *  screen (smallest width 600 dp: an unfolded foldable, a tablet): any way,
 *  filling the screen as it is held; a fixed orientation there makes Android
 *  shrink the app into a box (letterboxed, the rest of the screen blank). The
 *  game fits the picture to whatever shape it gets. Folding or unfolding
 *  while the app runs (configChanges keep the activity) asks again. (Both
 *  draw under a camera cutout on any edge: the theme, values-v30.) */
final class Screens {
    private Screens() {}

    static boolean large(Activity a) {
        return a.getResources().getConfiguration().smallestScreenWidthDp >= 600;
    }

    static void apply(Activity a) {
        int want = large(a) ? ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                            : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        if (a.getRequestedOrientation() != want) a.setRequestedOrientation(want);
    }
}
