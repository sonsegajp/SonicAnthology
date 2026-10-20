package io.github.sonsegajp.anthology;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
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
 * the same layers (title_art.py's ground, confetti, side band and logo) at
 * the art's own size, scaled up pixel for pixel to fill the screen. A phone
 * shows the art's 224 lines; a squarer screen (an unfolded foldable, a
 * tablet) gets taller art with every pattern carried on through it: the
 * ground's flat shapes are drawn here at any height, the confetti (its open
 * layer, no clearing for the logo) runs the whole sky and the side band its
 * whole length. The logo and the panel sit in the sky's middle, or, upright,
 * one above the other; the band stays at the foot. The confetti rises and the
 * band's teeth run as on the title. The menu runs along the band in the
 * title's lettering, the choice in gold, bobbing. Touch, or a pad.
 */
public class TitleView extends View {

    public interface Listener { void onMenu(int item); }

    static final int CHOOSE = 0, CD = 1, BUILD = 2, PLAY = 3;
    private static final String[] MENU = { "choose", "cd", "build", "play" };
    private static final int H = 224, BAND_TOP = 184, BAND_H = H - BAND_TOP, CONF_Y0 = 15, MIN_W = 400,
            STACK_W = 340, STACK_H = 380, PANEL_H = 156;
    // title_art.py's colours
    private static final int BLUE = 0xFF3157AE, GREEN = 0xFF57AE57, NAVY = 0xFF000031, GOLD = 0xFFFFCE31,
            RED = 0xFFFF3131, YELLOW = 0xFFFFFF57, BLACK = 0xFF000000;

    private final Map<String, Bitmap> img = new HashMap<>();
    private final Paint px = new Paint(), soft = new Paint(Paint.FILTER_BITMAP_FLAG), fill = new Paint(),
            line = new Paint();
    private final Path path = new Path();
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
    private float scale = 1, offX, offY;

    public TitleView(Context c) {
        super(c);
        setFocusable(true);
        setFocusableInTouchMode(true);
        px.setFilterBitmap(false);
        px.setAntiAlias(false);
        fill.setAntiAlias(false);
        line.setAntiAlias(false);
        line.setStyle(Paint.Style.STROKE);
        for (String n : new String[] { "title_confetti", "title_confetti_open", "title_side", "title_logo", "h_name",
                "h_years", "r_s1", "r_s2", "r_s3k", "r_scd", "r_optional", "s_need", "s_ready", "s_building",
                "s_done", "s_failed" })
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
        boolean stack = false;
        scale = vh / (float) H;                               /* a phone: the art's own 224 lines */
        if (vw / scale < MIN_W) {                             /* squarer: MIN_W across, taller */
            stack = (long) MIN_W * vh >= (long) STACK_H * vw; /* upright: the panel under the logo */
            scale = stack ? Math.min(vw / (float) STACK_W, vh / (float) STACK_H) : vw / (float) MIN_W;
        }
        int w = (int) Math.ceil(vw / scale), h = (int) Math.ceil(vh / scale);
        if (art == null || art.getWidth() != w || art.getHeight() != h) {
            art = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            ac = new Canvas(art);
        }
        offX = (vw - w * scale) / 2f;
        offY = (vh - h * scale) / 2f;
        scene(ac, w, h, stack);
        canvas.drawBitmap(art, new Rect(0, 0, w, h), new RectF(offX, offY, offX + w * scale, offY + h * scale), px);
        t++;
        postInvalidateOnAnimation();
    }

    private static int wrap(int v, int m) { v %= m; return v < 0 ? v + m : v; }

    private void rect(Canvas c, int x0, int y0, int x1, int y1, int argb) {
        fill.setColor(argb);
        c.drawRect(x0, y0, x1, y1, fill);
    }

    private void poly(Canvas c, int argb, float... xy) {
        path.reset();
        path.moveTo(xy[0], xy[1]);
        for (int i = 2; i < xy.length; i += 2) path.lineTo(xy[i], xy[i + 1]);
        path.close();
        fill.setColor(argb);
        c.drawPath(path, fill);
    }

    /* a ring of the given stroke inside the box x0,y0 .. x0+d,y0+d (PIL's outline) */
    private void oval(Canvas c, float x0, float y0, float d, float stroke, int argb) {
        line.setColor(argb);
        line.setStrokeWidth(stroke);
        float k = stroke / 2f;
        c.drawOval(new RectF(x0 + k, y0 + k, x0 + d + 1 - k, y0 + d + 1 - k), line);
    }

    private void scene(Canvas c, int w, int h, boolean stack) {
        int bandTop = h - BAND_H, cx = w / 2, x0 = (w - 640) / 2, sy = (bandTop - BAND_TOP) / 2;
        boolean tall = h > H;
        Bitmap conf = img.get(tall ? "title_confetti_open" : "title_confetti"), side = img.get("title_side");
        boolean margins = w >= 384 && side != null;          /* widescreen margins */
        int left = margins ? side.getWidth() : 0;

        // where the logo and the panel go
        Bitmap logo = img.get("title_logo");
        int lw = logo != null ? logo.getWidth() : 0, lh = logo != null ? logo.getHeight() : 0;
        int need = 0;                                         /* the panel's width: its widest row */
        for (String r : new String[] { "r_s1", "r_s2", "r_s3k", "r_scd" })
            if (img.get(r) != null) need = Math.max(need, img.get(r).getWidth());
        need += 30;
        float ls;
        int dw, dh, lx, ly, p0, p1, q0, q1, sunX, sunY, ringX, ringY;
        if (stack) {                                          /* the logo, the panel under it */
            ls = Math.min(1f, (w - 32) / (float) Math.max(1, lw));
            dw = Math.round(lw * ls);
            dh = Math.round(lh * ls);
            int pw = Math.min(w - 32, Math.max(need + 40, 220));
            int gap = Math.max(0, bandTop - 6 - 18 - dh - PANEL_H) / 3;
            lx = (w - dw) / 2;
            ly = 18 + gap;
            p0 = (w - pw) / 2;
            p1 = p0 + pw;
            q0 = ly + dh + gap;
            q1 = q0 + PANEL_H;
            sunX = lx + dw / 2 - 13;
            sunY = ly + dh / 2;
            ringX = p1 - 52;
            ringY = q0 + 100;
        } else {                                              /* side by side, in the sky's middle */
            ls = Math.min(1f, (w - 16 - need - (left + 6) - 8) / (float) Math.max(1, lw));
            dw = Math.round(lw * ls);
            dh = Math.round(lh * ls);
            lx = left + 6;
            ly = sy + 16 + (150 - 16 - dh) / 2;
            p0 = lx + dw + 8;
            p1 = w - 16;
            q0 = sy + 20;
            q1 = sy + 20 + PANEL_H;
            sunX = cx - 34;
            sunY = sy + 82;
            ringX = cx + 132;
            ringY = sy + 120;
        }

        // the ground (title_art.py's title_bg): paper; Sonic 1's colour wedges,
        // red from the upper right, green rising from the lower left; the sun
        // behind the logo, a hollow ring echoing it; the band and its furniture
        c.drawColor(Color.WHITE);
        poly(c, RED, cx + 186, 0, w + 40, 0, w + 40, bandTop, cx + 76, bandTop);
        poly(c, GREEN, -40, bandTop - 80, cx - 96, bandTop, -40, bandTop);
        fill.setColor(YELLOW);
        c.drawOval(new RectF(sunX - 76, sunY - 76, sunX + 77, sunY + 77), fill);
        oval(c, ringX, ringY, 40, 3, BLUE);
        oval(c, ringX + 1, ringY + 1, 40, 1, BLACK);
        for (int i = 0; i < 7; i++) rect(c, cx - 150 + i * 7, bandTop - 14, cx - 145 + i * 7, bandTop - 9, BLACK);
        rect(c, cx + 108, bandTop - 10, cx + 151, bandTop - 7, BLACK);
        rect(c, cx - 154, 13, cx + 154, 14, BLUE);            /* the header strip's rule */
        rect(c, 0, bandTop, w, h, BLUE);
        for (int sx : new int[] { cx - 152, cx + 140 }) {     /* white accents on the band */
            rect(c, sx, h - 10, sx + 5, h - 6, Color.WHITE);
            rect(c, sx + 7, h - 10, sx + 12, h - 6, Color.WHITE);
        }

        if (conf != null) {                                   /* rising, wrapped, CONF_Y0 to the band */
            int ch = conf.getHeight(), cw = conf.getWidth(), cl = (w - cw) / 2;
            for (int y = CONF_Y0, o = wrap(t / 3, ch); y < bandTop; o = 0) {
                int n = Math.min(ch - o, bandTop - y);
                c.drawBitmap(conf, new Rect(0, o, cw, o + n), new Rect(cl, y, cl + cw, y + n), px);
                y += n;
            }
        }
        for (int dy = 1; dy <= 5; dy++)                       /* the band's teeth, 10 apart, 5 high */
            for (int x = -10; x < w + 10; x += 10) {
                int k0 = wrap(x - x0 - t / 2, 10), base = x - k0;
                rect(c, base + dy, bandTop - dy, base + 10 - dy + 1, bandTop - dy + 1, BLUE);
            }
        if (margins) {
            if (tall) sideTall(c, side, h, bandTop);
            else c.drawBitmap(side, 0, 0, px);
            for (int row = 0; row < bandTop - 6; row++) {
                int y = wrap(row - t / 2, 14);
                int depth = 13 - (y < 7 ? 7 - y : y - 7) * 13 / 7;
                rect(c, w - depth, row, w, row + 1, GREEN);
            }
        }
        Bitmap hn = img.get("h_name"), hy = img.get("h_years");   /* the boxes' header strip */
        if (hn != null) c.drawBitmap(hn, cx - 154, 3, px);
        if (hy != null) c.drawBitmap(hy, cx + 154 - hy.getWidth(), 3, px);

        if (logo != null)
            c.drawBitmap(logo, new Rect(0, 0, lw, lh), new Rect(lx, ly, lx + dw, ly + dh), ls == 1f ? px : soft);
        panel(c, p0, q0, p1, q1);
        panelView.set(offX + (p0 + 4) * scale, offY + (q0 + 4) * scale, offX + (p1 - 4) * scale, offY + (q1 - 4) * scale);

        // the menu along the band
        int gap = 14, total = -gap;
        for (String m : MENU) total += img.get("m_" + m + "_w").getWidth() + gap;
        int x = (w - total) / 2;
        for (int i = 0; i < 4; i++) {
            Bitmap b = img.get("m_" + MENU[i] + (i == sel && enabled[i] ? "_g" : "_w"));
            int bob = i == sel && enabled[i] && ((t / 10) & 1) != 0 ? 1 : 0;
            int y = bandTop + 5 - bob;
            px.setAlpha(enabled[i] ? 255 : 95);
            c.drawBitmap(b, x, y, px);
            px.setAlpha(255);
            menuHit[i].set(x - gap / 2, bandTop - 4, x + b.getWidth() + gap / 2, h);
            x += b.getWidth() + gap;
        }
    }

    /* The left band down a taller screen: its black and teeth repeated (whole
     * teeth, so they stay in step), the word in the sky's middle, the squiggle
     * from above the band to the foot. */
    private void sideTall(Canvas c, Bitmap side, int h, int bandTop) {
        int sw = side.getWidth(), shift = bandTop - BAND_TOP, ws = shift / 2 / 10 * 10;
        for (int y = wrap(6 + ws, 10) - 10; y < h; y += 10)   /* (rows 6..16: one tooth, plain black) */
            c.drawBitmap(side, new Rect(0, 6, sw, 16), new Rect(0, y, sw, y + 10), px);
        c.drawBitmap(side, new Rect(0, 16, sw, BAND_TOP - 30), new Rect(0, 16 + ws, sw, BAND_TOP - 30 + ws), px);
        line.setColor(YELLOW);
        line.setStrokeWidth(2);
        float sx = 0, sy = 0;
        for (int y = bandTop - 30; y < h; y++) {
            float x = 14 + 4 * (float) Math.sin((y - shift) * 0.5);
            if (y > bandTop - 30) c.drawLine(sx, sy, x, y, line);
            sx = x;
            sy = y;
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
        int ax = (int) ((vx - offX) / scale), ay = (int) ((vy - offY) / scale);
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
