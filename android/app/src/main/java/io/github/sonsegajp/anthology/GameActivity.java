package io.github.sonsegajp.anthology;

import java.io.File;

import org.libsdl.app.SDLActivity;

/** The game: SDL's activity running libmain.so (the runner + Sonic Anthology).
 *  Its files (the built data, saves, settings) live in the app's external
 *  files dir, where the runner anchors itself (runner/main.c, __ANDROID__). */
public class GameActivity extends SDLActivity {
    @Override
    protected void onCreate(android.os.Bundle state) {
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
