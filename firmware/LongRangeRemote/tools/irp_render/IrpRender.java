// IrpRender - batch renderer for tools/build_db.py.
//
// Codes in protocols the firmware has no native encoder for (MCE, DirecTV, Dish, G.I. Cable, XMP,
// TiVo, Nokia32, Proton, ...) are rendered to raw timings by IrpTransmogrifier, the reference IRP
// implementation, so the firmware can send them as raw signals.
//
// stdin : one request per line:   <protocol>\t<D>\t<S>\t<F>\t<extra NAME=value,...>   (-1 = not given)
// Missing parameters (IRDB rows often leave S, X, E, PairID out) are set to 0.
// stdout: one answer per line:    OK\t<freq Hz>\t<duty 0..1 or empty>\t<intro us,...>\t<repeat us,...>
//                                 ERR\t<reason>
// Build:  javac -cp IrpTransmogrifier-1.2.14-jar-with-dependencies.jar IrpRender.java
// Run:    java -cp IrpTransmogrifier-1.2.14-jar-with-dependencies.jar:. IrpRender < in > out
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;
import org.harctoolbox.ircore.IrSignal;
import org.harctoolbox.irp.IrpDatabase;
import org.harctoolbox.irp.NameEngine;
import org.harctoolbox.irp.NamedProtocol;

public class IrpRender {
    public static void main(String[] args) throws Exception {
        IrpDatabase db = IrpDatabase.newDefaultIrpDatabase();
        Map<String, NamedProtocol> cache = new HashMap<>();
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        PrintWriter out = new PrintWriter(System.out);
        String line;
        while ((line = in.readLine()) != null) {
            String[] f = line.split("\t", -1);
            try {
                NamedProtocol p = cache.get(f[0]);
                if (p == null) {
                    p = db.getNamedProtocol(f[0]);
                    cache.put(f[0], p);
                }
                Map<String, Long> params = new HashMap<>();
                String[] names = {"D", "S", "F"};
                for (int i = 0; i < 3; i++) {
                    long v = Long.parseLong(f[i + 1]);
                    if (v >= 0) params.put(names[i], v);
                }
                if (f.length > 4 && !f[4].isEmpty()) {
                    for (String kv : f[4].split(",")) {
                        String[] a = kv.split("=");
                        params.put(a[0].trim(), Long.parseLong(a[1].trim()));
                    }
                }
                // Parameters IRDB leaves out (Nokia32 X, Apple PairID, 48-NEC E, ...) default to 0
                IrSignal s = null;
                for (int attempt = 0; s == null; attempt++) {
                    try {
                        s = p.render(new NameEngine(params));
                    } catch (org.harctoolbox.irp.NameUnassignedException e) {
                        java.util.regex.Matcher m = java.util.regex.Pattern.compile("Name (\\w+) not assigned")
                                .matcher(String.valueOf(e.getMessage()));
                        if (attempt >= 4 || !m.find()) throw e;
                        params.put(m.group(1), 0L);
                    }
                }
                StringBuilder b = new StringBuilder("OK\t");
                b.append(Math.round(s.getFrequencyWithDefault())).append('\t');
                if (s.getDutyCycle() != null) b.append(s.getDutyCycle());
                b.append('\t').append(join(s.getIntroInts())).append('\t').append(join(s.getRepeatInts()));
                out.println(b);
            } catch (Exception e) {
                out.println("ERR\t" + e.getClass().getSimpleName() + ": " + String.valueOf(e.getMessage()).replace('\t', ' ').replace('\n', ' '));
            }
        }
        out.flush();
    }

    private static String join(int[] a) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < a.length; i++) {
            if (i > 0) b.append(',');
            b.append(Math.abs(a[i]));
        }
        return b.toString();
    }
}
