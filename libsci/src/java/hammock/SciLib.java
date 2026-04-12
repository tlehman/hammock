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
import java.io.PrintWriter;
import java.io.StringWriter;

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
     * Captures everything SCI code writes to *out* (via println, print, etc.).
     * Bound into clojure.core/*out* at context init, then drained by
     * sciEvalString after each eval so the output gets returned alongside
     * the result. Without this, println throws because SCI's *out* is
     * unbound and can't be cast to java.io.Writer.
     */
    private static StringWriter captureBuffer;
    private static PrintWriter captureOut;

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
            // Allocate the capture buffer that will back SCI's *out*.
            // Referencing StringWriter/PrintWriter here also forces
            // GraalVM native-image static analysis to include them.
            captureBuffer = new StringWriter();
            captureOut = new PrintWriter(captureBuffer, true);

            Object shellNs = RT.map(
                Symbol.intern("exec"), SHELL_EXEC
            );
            // Expose the capture writer as a bound var so Clojure code
            // can rebind *out* to it via alter-var-root below.
            Object hammockIoNs = RT.map(
                Symbol.intern("capture-out"), captureOut
            );

            sciCtx = SCI_INIT.invoke(
                RT.map(
                    Keyword.intern("namespaces"),
                    RT.map(
                        Symbol.intern("hammock.shell"), shellNs,
                        Symbol.intern("hammock.io"), hammockIoNs
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

            // Drain any output from a previous eval.
            if (captureBuffer != null) {
                captureOut.flush();
                captureBuffer.getBuffer().setLength(0);
            }

            // Wrap user code in a binding form that rebinds *out* / *err*
            // to our capture writer. SCI's built-in *out* is read-only at
            // the root, but dynamic binding via (binding ...) is allowed
            // and gives println/print a valid Writer to cast to.
            //
            // Skip wrapping for file loads: those contain top-level (ns ...)
            // forms whose alias/require side-effects only fire when ns is
            // evaluated at top level, not nested inside (do ...). We detect
            // them by the leading `(ns` token.
            String trimmed = codeStr;
            int start = 0;
            while (start < trimmed.length() &&
                   Character.isWhitespace(trimmed.charAt(start))) {
                start++;
            }
            boolean isFileLoad =
                trimmed.regionMatches(start, "(ns ", 0, 4) ||
                trimmed.regionMatches(start, "(ns\n", 0, 4) ||
                trimmed.regionMatches(start, "(ns\t", 0, 4);

            String toEval;
            if (isFileLoad) {
                toEval = codeStr;
            } else {
                toEval = "(binding [*out* hammock.io/capture-out " +
                         "          *err* hammock.io/capture-out] " +
                         "  (do " + codeStr + "))";
            }

            Object result = SCI_EVAL_STRING.invoke(sciCtx, toEval);

            // Collect whatever the eval printed via (println ...) etc.
            String printed = "";
            if (captureBuffer != null) {
                captureOut.flush();
                printed = captureBuffer.toString();
                captureBuffer.getBuffer().setLength(0);
            }

            String resultStr;
            if (result == null) {
                resultStr = "nil";
            } else {
                resultStr = PR_STR.invoke(result).toString();
            }

            String combined = printed.isEmpty() ? resultStr : (printed + resultStr);
            return CTypeConversion.toCString(combined).get();
        } catch (Exception e) {
            /* Return the error text to the caller instead of printing it to
             * stderr, which would either (a) corrupt the ncurses display
             * during interactive use or (b) be silently discarded after the
             * child in sci_eval_interruptible closes stderr. The C side's
             * sci_result_is_error() detects the "sci_eval error:" prefix
             * and routes the text to *Messages* + minibuffer. */
            String msg = e.getMessage();
            if (msg == null) msg = e.getClass().getName();
            return CTypeConversion.toCString("sci_eval error: " + msg).get();
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
