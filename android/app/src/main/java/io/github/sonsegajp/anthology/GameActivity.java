package io.github.sonsegajp.anthology;

import java.io.File;

import org.libsdl.app.SDLActivity;

/** The game: SDL's activity running libmain.so (the runner + Sonic Anthology).
 *  Its files (the built data, saves, settings) live in the app's external
 *  files dir, where the runner anchors itself (runner/main.c, __ANDROID__). */
public class GameActivity extends SDLActivity {
    @Override
    protected void onCreate(android.os.Bundle state) {
        Screens.apply(this);
        File dir = getExternalFilesDir(null);
        if (!new File(dir, "sonic3k.bin").isFile() || !new File(dir, "trilogy.bin").isFile()) {
            /* (not built yet: to the setup) */
            startActivity(new android.content.Intent(this, SetupActivity.class));
            super.onCreate(state);
            finish();
            return;
        }
        super.onCreate(state);
    }

    /** SDL locks the orientation its hint names (landscape) as the window opens:
     *  on a large screen that letterboxes the game, so the screen decides */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        Screens.apply(this);
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration c) {
        super.onConfigurationChanged(c);
        Screens.apply(this);                        /* (folded or unfolded) */
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        File dir = getExternalFilesDir(null);
        return new String[] { new File(dir, "sonic3k.bin").getAbsolutePath() };
    }
}
