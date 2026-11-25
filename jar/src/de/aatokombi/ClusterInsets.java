package de.aatokombi;

/**
 * The insets template of Android Auto's map on the cluster (narrow / medium / wide), chosen on the
 * wheel (Config.ClusterMap.INSETS_KEY), and the default insets table.
 *
 * The libgal reads both files: /tsd/var/aatokombi/insets-template (the template's word) and the
 * table /tsd/var/aatokombi/app-insets, and sends the phone the line for the app on the cluster, the
 * template and the guidance state (shim/src/app_insets.h). The table of the build (app-insets.txt
 * next to Config.java) is installed when the unit has none, or still has an earlier build's default
 * table (its first line is HEADER). Java 1.3 source; never throws.
 */
public final class ClusterInsets {
    private ClusterInsets() {}

    public static final int NARROW = 0;
    public static final int MEDIUM = 1;
    public static final int WIDE = 2;
    private static final String[] NAMES = {"narrow", "medium", "wide"};

    static final String TEMPLATE_FILE = "/tsd/var/aatokombi/insets-template";
    static final String TABLE_FILE = "/tsd/var/aatokombi/app-insets";
    /** The first line of a default table: a table that still starts with it is replaced by the build's. */
    static final String HEADER = "# AAtoKombi default table";

    private static int current = -1;
    private static boolean installed;

    /** The template's word, as the table and the template file have it. */
    public static String name(int t) {
        return t >= 0 && t < NAMES.length ? NAMES[t] : "?";
    }

    /** The template word -> index, -1 for anything else. */
    static int parse(String s) {
        if (s == null) return -1;
        s = s.trim();
        for (int i = 0; i < NAMES.length; i++) {
            if (NAMES[i].equals(s)) return i;
        }
        return -1;
    }

    /** The template now: the file, else Config.ClusterMap.INSETS_TEMPLATE. */
    public static synchronized int current() {
        if (current < 0) {
            int t = parse(read(TEMPLATE_FILE));
            current = t >= 0 ? t : clamp(Config.ClusterMap.INSETS_TEMPLATE);
        }
        return current;
    }

    /** One template wider (+1) or narrower (-1); stops at narrow and wide. */
    public static void step(int dir) {
        set(current() + (dir > 0 ? 1 : -1));
    }

    /** The next template, after wide narrow again. */
    public static void next() {
        set((current() + 1) % NAMES.length);
    }

    static synchronized void set(int t) {
        t = clamp(t);
        if (t == current()) {
            ClusterVideo.log("insets template stays " + name(t));
            return;
        }
        current = t;
        write(TEMPLATE_FILE, name(t) + "\n");
        ClusterVideo.log("insets template -> " + name(t));
    }

    private static int clamp(int t) {
        return t < NARROW ? NARROW : t > WIDE ? WIDE : t;
    }

    /** Whether a table on the unit may be replaced by the build's: none, or a default one that differs. */
    static boolean replaceTable(String onUnit, String build) {
        if (onUnit == null) return true;
        return onUnit.startsWith(HEADER) && !onUnit.equals(build);
    }

    /**
     * Once per HMI start: the build's table onto the unit when replaceTable allows, and the template
     * file when there is none (the libgal needs a template to pick a line).
     */
    public static synchronized void install() {
        if (installed) return;
        installed = true;
        try {
            String build = AppInsetsDefault.TEXT;
            String unit = read(TABLE_FILE);
            if (replaceTable(unit, build)) {
                write(TABLE_FILE, build);
                ClusterVideo.log("insets table " + (unit == null ? "installed" : "updated to the build's default"));
            } else if (unit != null && !unit.startsWith(HEADER)) {
                ClusterVideo.log("insets table: the unit's own (no default header), kept");
            }
            if (parse(read(TEMPLATE_FILE)) < 0) {
                write(TEMPLATE_FILE, name(current()) + "\n");
            }
        } catch (Throwable t) {
            Quiet.note("ClusterInsets#1", t);
        }
    }

    /** The whole file as text, null when it is missing or unreadable. */
    private static String read(String path) {
        java.io.FileInputStream in = null;
        try {
            java.io.File f = new java.io.File(path);
            if (!f.exists()) return null;
            in = new java.io.FileInputStream(f);
            java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            byte[] b = new byte[1024];
            int n;
            while ((n = in.read(b)) > 0) o.write(b, 0, n);
            return new String(o.toByteArray(), "ISO-8859-1");
        } catch (Throwable t) {
            return null;
        } finally {
            try { if (in != null) in.close(); } catch (Throwable t) {}
        }
    }

    private static void write(String path, String text) {
        java.io.FileOutputStream o = null;
        try {
            o = new java.io.FileOutputStream(path);
            o.write(text.getBytes("ISO-8859-1"));
        } catch (Throwable t) {
            ClusterVideo.log("not written: " + path + ": " + t);
        } finally {
            try { if (o != null) o.close(); } catch (Throwable t) {}
        }
    }
}
