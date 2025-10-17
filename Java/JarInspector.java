import java.io.*;
import java.util.*;
import java.util.jar.*;

public class JarInspector {

    private static final String[] FLAG_A_KEYWORDS = {
        "autoclicker", "autoclick", "clicker", "keystrokesmod"
    };

    private static final String[] FLAG_B_KEYWORDS = {
        "xray", "fly", "aimbot", "killaura", "speedhack", "autobuild", "autopvp",
        "triggerbot", "bunnyhop", "nofall", "speedmine", "scaffold", 
        "jesusmod", "ghostclient", "freecam", "autorespawn", "flyhack", "getPlayerPOVHitResult"
    };

    private static final String[] FLAG_C_STRINGS = {
        "/5OFV7PFTIMB0V", "/net/java/a", "/net/java/b", "/net/java/c", "/net/java/d", "/net/java/e",
        "wurstclient", "meteor-client", "https://maven.aristois.net/manifest"
    };

    private static final long[] FLAG_C_LONGS = {
        -1083759330220665782L, -4062297973245990737L
    };

    public static String[] listClasses(String jarPath) throws IOException {
        boolean hasFlagA = false;
        boolean hasFlagB = false;
        boolean hasFlagC = false;

        try (JarFile jarFile = new JarFile(jarPath)) {
            Enumeration<JarEntry> entries = jarFile.entries();

            while (entries.hasMoreElements()) {
                JarEntry entry = entries.nextElement();

                if (entry.getName().endsWith(".class")) {
                    try (InputStream in = jarFile.getInputStream(entry)) {
                        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
                        byte[] data = new byte[4096];
                        int n;
                        while ((n = in.read(data)) != -1) {
                            buffer.write(data, 0, n);
                        }
                        byte[] classBytes = buffer.toByteArray();

                        StringBuilder result = new StringBuilder();
                        StringBuilder current = new StringBuilder();

                        for (byte b : classBytes) {
                            char c = (char) (b & 0xFF);
                            if (c >= 32 && c <= 126) {
                                current.append(c);
                            } else {
                                if (current.length() >= 4) {
                                    result.append(current).append(" ");
                                }
                                current.setLength(0);
                            }
                        }

                        if (current.length() >= 4) {
                            result.append(current);
                        }

                        String ascii = result.toString().toLowerCase();

                        for (String keyword : FLAG_A_KEYWORDS) {
                            if (ascii.contains(keyword)) {
                                hasFlagA = true;
                            }
                        }

                        for (String keyword : FLAG_B_KEYWORDS) {
                            if (ascii.contains(keyword)) {
                                hasFlagB = true;
                            }
                        }

                        for (String flagCStr : FLAG_C_STRINGS) {
                            if (ascii.contains(flagCStr)) {
                                hasFlagC = true;
                            }
                        }

                        if (!hasFlagC) {
                            for (long flagCLong : FLAG_C_LONGS) {
                                if (ascii.contains(Long.toString(flagCLong))) {
                                    hasFlagC = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        StringBuilder flags = new StringBuilder();
        if (hasFlagA) flags.append(" | Flag A");
        if (hasFlagB) flags.append(" | Flag B");
        if (hasFlagC) flags.append(" | Flag C");

        if (flags.length() > 0) {
            return new String[]{flags.toString()};
        } else {
            return new String[0];
        }
    }
}