package io.github.sonsegajp.anthology;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.RectF;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.util.TypedValue;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.chaquo.python.PyObject;
import com.chaquo.python.Python;
import com.chaquo.python.android.AndroidPlatform;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.CRC32;

/**
 * The app's front door: AnthologyBuilder for Android. The player chooses their
 * own dumps (Android's file picker: no storage permission), each recognised by
 * its CRC32 (the builder's list); optionally the folder of a Sonic CD BIN/CUE
 * dump. Build runs the PC builder's own converters (tools/trilogy, packed in
 * the APK, through Chaquopy's Python) into the game's files dir; Play starts
 * the game. The Sonic Origins sprites and the title art come in the APK.
 */
public class SetupActivity extends Activity {

    static final String MARK = "anthology-build.txt";   /* the build's version, once built */

    private static final int PICK_ROMS = 1, PICK_CD = 2;
    private static final long S1A = 0xAFE05EEEL, S1B = 0x18B1B998L, S2 = 0x24AB4C3AL,
            S3K = 0x63522553L, SK = 0x0658F691L, S3 = 0x9BC192CEL;

    private File gameDir, romDir, cdDir;
    private TextView log;
    private ScrollView logScroll;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean building;

    @Override
    protected void onCreate(Bundle state) {
        Screens.apply(this);
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        gameDir = getExternalFilesDir(null);
        romDir = new File(getFilesDir(), "roms");
        cdDir = new File(gameDir, "scd");
        romDir.mkdirs();
        buildUi();
        new Thread(() -> {
            try {
                unpackAssets();
                defaultSettings();
            } catch (IOException e) {
                say("Could not unpack the app's files: " + e);
            }
            ui.post(this::refresh);
        }).start();
    }

    // ---- screen ------------------------------------------------------------
    /* The title screen's look (TitleView), and over its panel the build's log
     * while it runs (or when there is something to say) */
    private TitleView title;
    private FrameLayout root;

    private void buildUi() {
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        title = new TitleView(this);
        title.setListener(item -> {
            if (item == TitleView.CHOOSE) pickRoms();
            else if (item == TitleView.CD) pickCd();
            else if (item == TitleView.BUILD) startBuild();
            else if (item == TitleView.PLAY) play();
        });
        root.addView(title, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        log = new TextView(this);
        log.setTextColor(Color.WHITE);
        log.setTextSize(11);
        log.setTypeface(Typeface.MONOSPACE);
        log.setShadowLayer(2, 1, 1, Color.BLACK);
        logScroll = new ScrollView(this);
        logScroll.addView(log);
        logScroll.setBackgroundColor(0xF0000031);
        logScroll.setVisibility(View.GONE);
        logScroll.setOnClickListener(v -> { if (!building) logScroll.setVisibility(View.GONE); });
        log.setOnClickListener(v -> { if (!building) logScroll.setVisibility(View.GONE); });
        root.addView(logScroll, new FrameLayout.LayoutParams(1, 1));
        title.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> placeLog());
        setContentView(root);
        title.requestFocus();
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    /* the log over the title's panel (its rect is known once the title has drawn) */
    private void placeLog() {
        title.post(() -> {
            RectF r = title.panelView;
            if (r.width() < 4) { ui.postDelayed(this::placeLog, 50); return; }
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams((int) r.width(), (int) r.height());
            lp.leftMargin = (int) r.left;
            lp.topMargin = (int) r.top;
            logScroll.setLayoutParams(lp);
            logScroll.setPadding(dp(8), dp(6), dp(8), dp(6));
        });
    }

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private String lastStatus;

    /* a note over the panel: it stays while a build runs, else it goes after a while */
    private final Runnable hideLog = () -> { if (!building && !"failed".equals(lastStatus)) logScroll.setVisibility(View.GONE); };

    private void say(String s) {
        ui.post(() -> {
            if (logScroll.getVisibility() != View.VISIBLE && !building) log.setText("");
            logScroll.setVisibility(View.VISIBLE);
            log.append(s.endsWith("\n") ? s : s + "\n");
            logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN));
            ui.removeCallbacks(hideLog);
            if (!building) ui.postDelayed(hideLog, 5000);
        });
    }

    // ---- state -------------------------------------------------------------
    private File rom(String kind) { return new File(romDir, kind + ".bin"); }

    private boolean haveS3k() { return rom("s3k").isFile() || (rom("sk").isFile() && rom("s3").isFile()); }

    private File cue() {
        File[] fs = cdDir.listFiles();
        if (fs != null)
            for (File f : fs)
                if (f.getName().toLowerCase().endsWith(".cue")) return f;
        return null;
    }

    private boolean built() {
        File m = new File(gameDir, MARK);
        return m.isFile() && new File(gameDir, "sonic3k.bin").isFile() && new File(gameDir, "trilogy.bin").isFile()
                && versionName().equals(readText(m).trim());
    }

    private void refresh() {
        boolean ready = rom("s1").isFile() && rom("s2").isFile() && haveS3k();
        boolean ok = built();
        title.have[0] = rom("s1").isFile();
        title.have[1] = rom("s2").isFile();
        title.have[2] = haveS3k();
        title.have[3] = cue() != null;
        title.enabled[TitleView.CHOOSE] = !building;
        title.enabled[TitleView.CD] = !building;
        title.enabled[TitleView.BUILD] = ready && !building;
        title.enabled[TitleView.PLAY] = ok && !building;
        String st = building ? "building" : ok ? "done" : "failed".equals(lastStatus) ? "failed"
                : ready ? "ready" : "need";
        title.status = st;
        if (!st.equals(lastStatus))                       /* (the menu's choice follows the next step) */
            title.select(st.equals("done") ? TitleView.PLAY : st.equals("ready") ? TitleView.BUILD
                    : st.equals("building") ? TitleView.PLAY : TitleView.CHOOSE);
        lastStatus = st;
    }

    // ---- choosing ------------------------------------------------------------
    private void pickRoms() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, PICK_ROMS);
    }

    private void pickCd() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_CD);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null) return;
        if (request == PICK_ROMS) {
            List<Uri> uris = new ArrayList<>();
            if (data.getClipData() != null)
                for (int k = 0; k < data.getClipData().getItemCount(); k++)
                    uris.add(data.getClipData().getItemAt(k).getUri());
            else if (data.getData() != null) uris.add(data.getData());
            new Thread(() -> { for (Uri u : uris) takeRom(u); ui.post(this::refresh); }).start();
        } else if (request == PICK_CD && data.getData() != null) {
            Uri tree = data.getData();
            building = true;
            refresh();
            new Thread(() -> { takeCd(tree); building = false; ui.post(this::refresh); }).start();
        }
    }

    private void takeRom(Uri u) {
        String name = displayName(u);
        try {
            File tmp = new File(romDir, "incoming.bin");
            copy(getContentResolver().openInputStream(u), tmp);
            long len = tmp.length();
            int skip = len % 0x4000 == 512 ? 512 : 0;            /* (a copier header) */
            long crc = crc32(tmp, skip);
            String kind = crc == S1A || crc == S1B ? "s1" : crc == S2 ? "s2" : crc == S3K ? "s3k"
                    : crc == SK ? "sk" : crc == S3 ? "s3" : null;
            if (kind == null) {
                tmp.delete();
                say(name + ": not a dump Sonic Anthology knows (CRC32 " + String.format("%08X", crc)
                        + "). Sonic 1 and 2 need the REV01 dumps.");
                return;
            }
            File dst = rom(kind);
            dst.delete();
            if (!tmp.renameTo(dst)) throw new IOException("rename");
            String[] kinds = { "s1", "s2", "s3k", "sk", "s3" };
            String[] names = { "Sonic the Hedgehog", "Sonic the Hedgehog 2", "Sonic 3 & Knuckles",
                    "Sonic & Knuckles", "Sonic the Hedgehog 3" };
            say(name + ": " + names[java.util.Arrays.asList(kinds).indexOf(kind)]);
        } catch (IOException e) {
            say(name + ": " + e);
        }
    }

    private void takeCd(Uri tree) {
        ContentResolver cr = getContentResolver();
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        List<String[]> files = new ArrayList<>();   /* id, name */
        try (Cursor c = cr.query(kids, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            while (c != null && c.moveToNext()) files.add(new String[] { c.getString(0), c.getString(1) });
        }
        boolean hasCue = false;
        for (String[] f : files) hasCue |= f[1].toLowerCase().endsWith(".cue");
        if (!hasCue) { say("That folder has no .cue: choose the folder of Sonic CD's BIN/CUE dump."); return; }
        deleteTree(cdDir);
        cdDir.mkdirs();
        for (String[] f : files) {
            String low = f[1].toLowerCase();
            if (!(low.endsWith(".cue") || low.endsWith(".bin") || low.endsWith(".img") || low.endsWith(".iso")
                    || low.endsWith(".wav"))) continue;
            say("Copying " + f[1] + "…");
            try {
                copy(cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, f[0])), new File(cdDir, f[1]));
            } catch (IOException e) {
                say(f[1] + ": " + e);
                deleteTree(cdDir);
                return;
            }
        }
        say("Sonic CD's disc is in. Build to add its zones.");
    }

    // ---- building --------------------------------------------------------------
    private void startBuild() {
        building = true;
        refresh();
        log.setText("");
        logScroll.setVisibility(View.VISIBLE);
        new File(gameDir, MARK).delete();
        final File logFile = new File(gameDir, "builder.log");
        logFile.delete();
        final Runnable tail = new Runnable() {
            int shown = 0;
            @Override public void run() {
                String t = readText(logFile);
                if (t.length() > shown) { log.append(t.substring(shown)); shown = t.length();
                    logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN)); }
                if (building) ui.postDelayed(this, 500);
            }
        };
        ui.post(tail);
        new Thread(() -> {
            int rc = -1;
            try {
                if (!Python.isStarted()) Python.start(new AndroidPlatform(this));
                Python py = Python.getInstance();
                py.getModule("sys").get("path").callAttr("insert", 0,
                        new File(getFilesDir(), "builder/builder").getAbsolutePath());
                List<String> args = new ArrayList<>();
                args.add("--s1"); args.add(rom("s1").getAbsolutePath());
                args.add("--s2"); args.add(rom("s2").getAbsolutePath());
                if (rom("s3k").isFile()) { args.add("--s3k"); args.add(rom("s3k").getAbsolutePath()); }
                else { args.add("--sk"); args.add(rom("sk").getAbsolutePath());
                       args.add("--s3"); args.add(rom("s3").getAbsolutePath()); }
                File cue = cue();
                if (cue != null) { args.add("--scd"); args.add(cue.getAbsolutePath()); }
                PyObject r = py.getModule("android_entry").callAttr("build", gameDir.getAbsolutePath(),
                        args.toArray(new String[0]));
                rc = r == null ? -1 : r.toInt();
            } catch (Throwable e) {
                say("The build stopped: " + e);
            }
            final int code = rc;
            if (code == 0) writeText(new File(gameDir, MARK), versionName() + "\n");
            ui.postDelayed(() -> {
                building = false;
                tail.run();
                if (code == 0) {
                    logScroll.setVisibility(View.GONE);          /* (the panel says ALL SET) */
                } else {
                    log.append("\nThe build failed (see above). Tap here to close.\n");
                    logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN));
                    lastStatus = "failed";
                }
                refresh();
            }, 600);
        }).start();
    }

    private void play() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    // ---- the APK's own files -------------------------------------------------
    /* builder/: the converters (filesDir/builder: Python reads them as files);
     * game/: the Origins sprites pack and the title art (the game's dir).
     * Unpacked again whenever the app is updated. */
    private void unpackAssets() throws IOException {
        SharedPreferences p = getSharedPreferences("anthology", MODE_PRIVATE);
        long stamp = packageInfo().lastUpdateTime;
        if (p.getLong("assets", 0) == stamp && new File(getFilesDir(), "builder/builder/android_entry.py").isFile())
            return;
        deleteTree(new File(getFilesDir(), "builder"));
        unpack(getAssets(), "builder", new File(getFilesDir(), "builder"));
        unpack(getAssets(), "game", gameDir);
        p.edit().putLong("assets", stamp).apply();
    }

    /* A phone's first settings.ini: fullscreen (SDL only goes immersive for a
     * window made fullscreen) and the widescreen picture that fills the screen.
     * The player's own changes are kept: written only when there is none. */
    private void defaultSettings() {
        File f = new File(gameDir, "settings.ini");
        if (f.isFile()) return;
        writeText(f, "[video]\nwindow_scale = 1\nfullscreen = 1\nlinear_filter = 0\nwidescreen = 0\n"
                + "widescreen_cells = 8\n\n[audio]\nvolume = 100\n\n[mods.widescreen]\nenabled = 1\n"
                + "aspect = adaptive\n");
    }

    private void unpack(AssetManager am, String path, File dst) throws IOException {
        String[] kids = am.list(path);
        if (kids == null || kids.length == 0) {
            dst.getParentFile().mkdirs();
            copy(am.open(path), dst);
            return;
        }
        dst.mkdirs();
        for (String k : kids) unpack(am, path + "/" + k, new File(dst, k));
    }

    // ---- small helpers -----------------------------------------------------------
    private PackageInfo packageInfo() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0); }
        catch (Exception e) { throw new RuntimeException(e); }
    }

    private String versionName() { return packageInfo().versionName; }

    private String displayName(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[] { "_display_name" }, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) { }
        return u.getLastPathSegment();
    }

    static void copy(InputStream in, File dst) throws IOException {
        if (in == null) throw new IOException("unreadable");
        File part = new File(dst.getPath() + ".part");
        try (InputStream i = in; OutputStream o = new FileOutputStream(part)) {
            byte[] b = new byte[1 << 16];
            for (int n; (n = i.read(b)) > 0; ) o.write(b, 0, n);
        }
        dst.delete();
        if (!part.renameTo(dst)) throw new IOException("rename " + dst);
    }

    private static long crc32(File f, int skip) throws IOException {
        CRC32 c = new CRC32();
        try (InputStream i = new FileInputStream(f)) {
            if (skip > 0 && i.skip(skip) != skip) throw new IOException("short");
            byte[] b = new byte[1 << 16];
            for (int n; (n = i.read(b)) > 0; ) c.update(b, 0, n);
        }
        return c.getValue();
    }

    static String readText(File f) {
        try (InputStream i = new FileInputStream(f)) {
            byte[] b = new byte[(int) f.length()];
            int n = 0;
            while (n < b.length) { int r = i.read(b, n, b.length - n); if (r < 0) break; n += r; }
            return new String(b, 0, n, "UTF-8");
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File f, String s) {
        try (OutputStream o = new FileOutputStream(f)) { o.write(s.getBytes("UTF-8")); } catch (IOException ignored) { }
    }

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration c) {
        super.onConfigurationChanged(c);
        Screens.apply(this);                        /* (folded or unfolded) */
    }
}
