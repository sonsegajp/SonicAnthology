package io.github.sonsegajp.anthology;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * The setup screen, drawn as the game's title screen is (trilogy_title.c):
 * the same layers (title_art.py's ground, confetti, side band and logo), at
 * the art's own 224 lines, scaled up pixel for pixel to fill the screen;
 * the confetti rises and the band's teeth run as there. The logo sits left,
 * a panel right lists the four games; the menu runs along the blue band in
 * the title's lettering, the choice in gold, bobbing. Touch, or a pad.
 */
public class TitleView extends View {

    public interface Listener { void onMenu(int item); }

    static final int CHOOSE = 0, CD = 1, BUILD = 2, PLAY = 3;
    private static final String[] MENU = { "choose", "cd", "build", "play" };
    private static final int H = 224, BAND_TOP = 184, CONF_Y0 = 15;
    private static final int BLUE = 0xFF3157AE, GREEN = 0xFF57AE57, NAVY = 0xFF000031, GOLD = 0xFFFFCE31;

    private final Map<String, Bitmap> img = new HashMap<>();
    private final Paint px = new Paint(), soft = new Paint(Paint.FILTER_BITMAP_FLAG), fill = new Paint();
    private Bitmap art;
    private Canvas ac;
    private int t, sel;
    private Listener listener;

    // state from the activity
    boolean[] have = new boolean[4];              /* s1 s2 s3k scd */
    boolean[] enabled = { true, true, false, false };
    String status = "need";
    /* the panel, in view pixels (the activity lays the log over it) */
    final RectF panelView = new RectF();
    private final Rect[] menuHit = new Rect[4];
    private float scale = 1, offX;

    public TitleView(Context c) {
        super(c);
        setFocusable(true);
        setFocusableInTouchMode(true);
        px.setFilterBitmap(false);
        px.setAntiAlias(false);
        fill.setAntiAlias(false);
        for (String n : new String[] { "title_bg", "title_confetti", "title_side", "title_logo", "h_name", "h_years",
                "r_s1", "r_s2", "r_s3k", "r_scd", "r_optional", "s_need", "s_ready", "s_building", "s_done",
                "s_failed" })
            img.put(n, load(c, n));
        for (String m : MENU) { img.put("m_" + m + "_w", load(c, "m_" + m + "_w")); img.put("m_" + m + "_g", load(c, "m_" + m + "_g")); }
        for (int i = 0; i < 4; i++) menuHit[i] = new Rect();
    }

    private static Bitmap load(Context c, String n) {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inScaled = false;
        try (InputStream in = c.getAssets().open("ui/" + n + ".png")) {
            return BitmapFactory.decodeStream(in, null, o);
        } catch (IOException e) {
            return null;
        }
    }

    void setListener(Listener l) { listener = l; }

    // ---- drawing ---------------------------------------------------------------
    @Override
    protected void onDraw(Canvas canvas) {
        int vw = getWidth(), vh = getHeight();
        if (vw <= 0 || vh <= 0) return;
        scale = vh / (float) H;
        int w = (int) Math.ceil(vw / scale);
        if (art == null || art.getWidth() != w) {
            art = Bitmap.createBitmap(w, H, Bitmap.Config.ARGB_8888);
            ac = new Canvas(art);
        }
        scene(ac, w);
        offX = (vw - w * scale) / 2f;
        canvas.drawBitmap(art, new Rect(0, 0, w, H), new RectF(offX, 0, offX + w * scale, vh), px);
        t++;
        postInvalidateOnAnimation();
    }

    private static int wrap(int v, int m) { v %= m; return v < 0 ? v + m : v; }

    private void rect(Canvas c, int x0, int y0, int x1, int y1, int argb) {
        fill.setColor(argb);
        c.drawRect(x0, y0, x1, y1, fill);
    }

    private void scene(Canvas c, int w) {
        Bitmap bg = img.get("title_bg"), conf = img.get("title_confetti"), side = img.get("title_side");
        c.drawColor(Color.WHITE);
        int x0 = (w - (bg != null ? bg.getWidth() : w)) / 2;
        if (bg != null) c.drawBitmap(bg, x0, 0, px);
        if (conf != null) {                                   /* rising, wrapped (CONF_Y0..) */
            int ch = conf.getHeight(), cw = conf.getWidth(), o = wrap(t / 3, ch), cx = (w - cw) / 2;
            c.drawBitmap(conf, new Rect(0, o, cw, ch), new Rect(cx, CONF_Y0, cx + cw, CONF_Y0 + ch - o), px);
            if (o > 0) c.drawBitmap(conf, new Rect(0, 0, cw, o), new Rect(cx, CONF_Y0 + ch - o, cx + cw, CONF_Y0 + ch), px);
        }
        for (int dy = 1; dy <= 5; dy++)                       /* the band's teeth, 10 apart, 5 high */
            for (int x = -10; x < w + 10; x += 10) {
                int k0 = wrap(x - x0 - t / 2, 10), base = x - k0;
                rect(c, base + dy, BAND_TOP - dy, base + 10 - dy + 1, BAND_TOP - dy + 1, BLUE);
            }
        if (w >= 384 && side != null) {                       /* widescreen margins */
            c.drawBitmap(side, 0, 0, px);
            for (int line = 0; line < BAND_TOP - 6; line++) {
                int y = wrap(line - t / 2, 14);
                int depth = 13 - (y < 7 ? 7 - y : y - 7) * 13 / 7;
                rect(c, w - depth, line, w, line + 1, GREEN);
            }
        }
        int cx = w / 2;                                       /* the boxes' header strip */
        Bitmap hn = img.get("h_name"), hy = img.get("h_years");
        if (hn != null) c.drawBitmap(hn, cx - 154, 3, px);
        if (hy != null) c.drawBitmap(hy, cx + 154 - hy.getWidth(), 3, px);

        // the logo, left; the panel, right
        Bitmap logo = img.get("title_logo");
        int lw = logo != null ? logo.getWidth() : 0, lh = logo != null ? logo.getHeight() : 0;
        int left = side != null && w >= 384 ? side.getWidth() : 0;
        int need = 0;                                         /* the panel's width: its widest row */
        for (String r : new String[] { "r_s1", "r_s2", "r_s3k", "r_scd" })
            if (img.get(r) != null) need = Math.max(need, img.get(r).getWidth());
        need += 30;
        float ls = Math.min(1f, (w - 16 - need - (left + 6) - 8) / (float) Math.max(1, lw));
        int dw = Math.round(lw * ls), dh = Math.round(lh * ls);
        int lx = left + 6, ly = 16 + (150 - 16 - dh) / 2;
        if (logo != null)
            c.drawBitmap(logo, new Rect(0, 0, lw, lh), new Rect(lx, ly, lx + dw, ly + dh), ls == 1f ? px : soft);
        int p0 = lx + dw + 8, p1 = w - 16, q0 = 20, q1 = 176;
        panel(c, p0, q0, p1, q1);
        panelView.set(offX + (p0 + 4) * scale, (q0 + 4) * scale, offX + (p1 - 4) * scale, (q1 - 4) * scale);

        // the menu along the band
        int gap = 14, total = -gap;
        for (String m : MENU) total += img.get("m_" + m + "_w").getWidth() + gap;
        int x = (w - total) / 2;
        for (int i = 0; i < 4; i++) {
            Bitmap b = img.get("m_" + MENU[i] + (i == sel && enabled[i] ? "_g" : "_w"));
            int bob = i == sel && enabled[i] && ((t / 10) & 1) != 0 ? 1 : 0;
            int y = BAND_TOP + 5 - bob;
            px.setAlpha(enabled[i] ? 255 : 95);
            c.drawBitmap(b, x, y, px);
            px.setAlpha(255);
            menuHit[i].set(x - gap / 2, BAND_TOP - 4, x + b.getWidth() + gap / 2, H);
            x += b.getWidth() + gap;
        }
    }

    /* Sonic 3's slab: navy glass, a white keyline inset; the games, each with a ring */
    private void panel(Canvas c, int x0, int y0, int x1, int y1) {
        rect(c, x0, y0, x1, y1, 0xEC000031);
        rect(c, x0 + 2, y0 + 2, x1 - 2, y0 + 3, 0xFFFFFFFF);
        rect(c, x0 + 2, y1 - 3, x1 - 2, y1 - 2, 0xFFFFFFFF);
        rect(c, x0 + 2, y0 + 2, x0 + 3, y1 - 2, 0xFFFFFFFF);
        rect(c, x1 - 3, y0 + 2, x1 - 2, y1 - 2, 0xFFFFFFFF);
        String[] rows = { "r_s1", "r_s2", "r_s3k", "r_scd" };
        int y = y0 + 12;
        for (int i = 0; i < 4; i++) {
            ring(c, x0 + 12, y + 5, have[i]);
            Bitmap b = img.get(rows[i]);
            if (b != null) c.drawBitmap(b, x0 + 22, y, px);
            if (i == 3 && !have[3]) {
                Bitmap o = img.get("r_optional");
                if (o != null && x0 + 26 + b.getWidth() + o.getWidth() < x1 - 6) {
                    px.setAlpha(140);
                    c.drawBitmap(o, x0 + 26 + b.getWidth(), y, px);
                    px.setAlpha(255);
                }
            }
            y += 20;
        }
        Bitmap s = img.get("s_" + status);
        boolean blink = "building".equals(status) && ((t / 20) & 1) != 0;
        if (s != null && !blink) c.drawBitmap(s, (x0 + x1 - s.getWidth()) / 2, y1 - 24, px);
    }

    /* a ring: gold when the game is in, a grey outline when not */
    private void ring(Canvas c, int cx, int cy, boolean on) {
        int col = on ? GOLD : 0xFF7080A0;
        int[] row = { 2, 4, 5, 5, 5, 5, 4, 2 };              /* half-widths, 8 rows: a pixel ring */
        for (int r = 0; r < 8; r++) {
            int hw = row[r], y = cy - 4 + r;
            rect(c, cx - hw, y, cx + hw, y + 1, col);
            if (r >= 2 && r <= 5) rect(c, cx - hw + 2, y, cx + hw - 2, y + 1, on ? 0xFF8A5A00 : 0xEC000031);
        }
        if (on) rect(c, cx - 3, cy - 3, cx - 1, cy - 2, 0xFFFFFFFF);   /* the shine */
    }

    // ---- input -----------------------------------------------------------------------
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (e.getAction() == MotionEvent.ACTION_DOWN || e.getAction() == MotionEvent.ACTION_MOVE) {
            int i = hit(e.getX(), e.getY());
            if (i >= 0) sel = i;
            return true;
        }
        if (e.getAction() == MotionEvent.ACTION_UP) {
            int i = hit(e.getX(), e.getY());
            if (i >= 0) activate(i);
            performClick();
            return true;
        }
        return super.onTouchEvent(e);
    }

    @Override
    public boolean performClick() { return super.performClick(); }

    private int hit(float vx, float vy) {
        int ax = (int) ((vx - offX) / scale), ay = (int) (vy / scale);
        for (int i = 0; i < 4; i++) if (menuHit[i].contains(ax, ay)) return i;
        return -1;
    }

    private void activate(int i) {
        sel = i;
        if (enabled[i] && listener != null) listener.onMenu(i);
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        switch (code) {
        case KeyEvent.KEYCODE_DPAD_LEFT: sel = (sel + 3) % 4; return true;
        case KeyEvent.KEYCODE_DPAD_RIGHT: sel = (sel + 1) % 4; return true;
        case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_BUTTON_A:
        case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_BUTTON_X: case KeyEvent.KEYCODE_BUTTON_Y:
            activate(sel);
            return true;
        default:
            return super.onKeyDown(code, e);
        }
    }

    void select(int i) { sel = i; }
}
