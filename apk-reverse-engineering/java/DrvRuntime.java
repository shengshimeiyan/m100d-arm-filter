package com.dynamixsoftware.drv;
public abstract class DrvRuntime {
    private static final java.util.Hashtable drivers;

    static DrvRuntime()
    {
        com.dynamixsoftware.drv.DrvRuntime.drivers = new java.util.Hashtable();
        return;
    }

    public DrvRuntime()
    {
        return;
    }

    public static com.dynamixsoftware.drv.a exec(String[] p9, String[] p10)
    {
        long v0_1 = new java.io.File(p9[0]);
        String[] v2_6 = v0_1.getName().split("\\.")[0];
        int v3_0 = com.dynamixsoftware.drv.DrvRuntime.drivers;
        com.dynamixsoftware.drv.DrvRuntime v4_1 = ((com.dynamixsoftware.drv.DrvRuntime) v3_0.get(v2_6));
        if (v4_1 == null) {
            System.load(v0_1.getAbsolutePath());
            com.dynamixsoftware.drv.DrvRuntime v4_4 = new StringBuilder();
            v4_4.append(com.dynamixsoftware.drv.DrvRuntime.getName());
            v4_4.append("$");
            v4_4.append(v2_6);
            v4_1 = ((com.dynamixsoftware.drv.DrvRuntime) Class.forName(v4_4.toString()).newInstance());
            v3_0.put(v2_6, v4_1);
        }
        if (p10 == null) {
            p10 = new String[0];
        }
        String[] v2_3 = new String[(p10.length * 2)];
        int v3_1 = 0;
        while (v3_1 < p10.length) {
            String v5_6;
            String v5_5 = p10[v3_1].split("=");
            int v6_1 = (v3_1 * 2);
            v2_3[v6_1] = v5_5[0];
            if (v5_5.length <= 1) {
                v5_6 = "";
            } else {
                v5_6 = v5_5[1];
            }
            v2_3[(v6_1 + 1)] = v5_6;
            v3_1++;
        }
        Runtime.getRuntime();
        int[] v10_2 = new int[3];
        return new com.dynamixsoftware.drv.a(v4_1, v4_1.procExec(p9, v2_3, v0_1.getParentFile().getAbsolutePath(), v10_2), v10_2);
    }

    abstract void procDestroy(long p0);

    abstract long procExec(String[] p0, String[] p1, String p2, int[] p3);

    abstract int procWait(long p0);
}
