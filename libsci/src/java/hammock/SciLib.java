package hammock;

import org.graalvm.nativeimage.IsolateThread;
import org.graalvm.nativeimage.c.function.CEntryPoint;
import org.graalvm.nativeimage.c.type.CCharPointer;
import org.graalvm.nativeimage.c.type.CTypeConversion;

import clojure.lang.AFn;
import clojure.lang.IFn;
import clojure.lang.Keyword;
import clojure.lang.PersistentHashMap;
import clojure.lang.RT;
import clojure.lang.Symbol;
import clojure.lang.Var;

import java.io.BufferedReader;
import java.io.InputStreamReader;

/**
 * C-callable entry points for the SCI (Small Clojure Interpreter).
 *
 * Build-time vs runtime initialization:
 * - Clojure RT and SCI are initialized at BUILD TIME (--initialize-at-build-time)
 *   so their class loaders can find .clj source files.
 * - SCI context is created at RUNTIME via sci_init().
 * - SCI function references are resolved at BUILD TIME and cached.
 */
public class SciLib {

    // Resolve SCI functions at build time (class init runs during native-image build)
    private static final IFn SCI_INIT;
    private static final IFn SCI_EVAL_STRING;
    private static final IFn PR_STR;

    static {
        try {
            // Force SCI namespace loading at build time
            RT.var("clojure.core", "require").invoke(Symbol.intern("sci.core"));
            SCI_INIT = RT.var("sci.core", "init");
            SCI_EVAL_STRING = RT.var("sci.core", "eval-string*");
            PR_STR = RT.var("clojure.core", "pr-str");
        } catch (Exception e) {
            throw new RuntimeException("Failed to initialize SCI at build time", e);
        }
    }

    /** SCI evaluation context (created at runtime) */
    private static Object sciCtx;

    /**
     * Shell exec function exposed to SCI as hammock.shell/exec.
     * Takes a vector of strings (command + args), returns
     * {:out "stdout" :err "stderr" :exit 0}
     */
    private static final IFn SHELL_EXEC = new AFn() {
        @Override
        public Object invoke(Object args) {
            try {
                @SuppressWarnings("unchecked")
                java.util.List<Object> argList = (java.util.List<Object>) args;
                String[] cmd = new String[argList.size()];
                for (int i = 0; i < argList.size(); i++) {
                    cmd[i] = argList.get(i).toString();
                }

                ProcessBuilder pb = new ProcessBuilder(cmd);
                pb.redirectErrorStream(false);
                Process proc = pb.start();

                String out = new String(proc.getInputStream().readAllBytes());
                String err = new String(proc.getErrorStream().readAllBytes());
                int exit = proc.waitFor();

                return RT.map(
                    Keyword.intern("out"), out,
                    Keyword.intern("err"), err,
                    Keyword.intern("exit"), (long) exit
                );
            } catch (Exception e) {
                return RT.map(
                    Keyword.intern("out"), "",
                    Keyword.intern("err"), e.getMessage(),
                    Keyword.intern("exit"), -1L
                );
            }
        }
    };

    @CEntryPoint(name = "libsci_init")
    public static int sciInit(IsolateThread thread) {
        try {
            // Build namespace map with hammock.shell/exec
            Object shellNs = RT.map(
                Symbol.intern("exec"), SHELL_EXEC
            );

            sciCtx = SCI_INIT.invoke(
                RT.map(
                    Keyword.intern("namespaces"),
                    RT.map(
                        Symbol.intern("hammock.shell"), shellNs
                    )
                )
            );
            return 0;
        } catch (Exception e) {
            System.err.println("sci_init failed: " + e.getMessage());
            e.printStackTrace();
            return -1;
        }
    }

    @CEntryPoint(name = "libsci_eval_string")
    public static CCharPointer sciEvalString(IsolateThread thread, CCharPointer code) {
        try {
            String codeStr = CTypeConversion.toJavaString(code);
            Object result = SCI_EVAL_STRING.invoke(sciCtx, codeStr);
            if (result == null) {
                return CTypeConversion.toCString("nil").get();
            }
            String resultStr = PR_STR.invoke(result).toString();
            return CTypeConversion.toCString(resultStr).get();
        } catch (Exception e) {
            System.err.println("sci_eval error: " + e.getMessage());
            return CTypeConversion.toCString("nil").get();
        }
    }

    @CEntryPoint(name = "libsci_get_state_version")
    public static long sciGetStateVersion(IsolateThread thread) {
        try {
            Object result = SCI_EVAL_STRING.invoke(sciCtx, "@hammock.state/*config-version*");
            if (result instanceof Number) {
                return ((Number) result).longValue();
            }
            return 0;
        } catch (Exception e) {
            return 0;
        }
    }

    @CEntryPoint(name = "libsci_free_string")
    public static void sciFreeString(IsolateThread thread, CCharPointer str) {
        // API contract placeholder
    }

    @CEntryPoint(name = "libsci_shutdown")
    public static void sciShutdown(IsolateThread thread) {
        sciCtx = null;
    }
}
