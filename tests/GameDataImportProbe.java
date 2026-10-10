package org.burstlimit.android;

import java.io.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Runs the exact Android importer on the host; fixtures supply the user's XEX. */
public final class GameDataImportProbe {
    public static void main(String[] args) {
        if (args[0].equals("verify")) { System.exit(GameDataImporter.verify(new File(args[1])) ? 0 : 1); }
        AtomicBoolean cancel = new AtomicBoolean();
        GameDataImporter.Progress progress = (done, total, file) -> {
            if (args.length == 3 && done >= Long.parseLong(args[2])) cancel.set(true);
        };
        try {
            if (args[0].endsWith(".iso")) {
                try (FileInputStream input = new FileInputStream(args[0])) {
                    GameDataImporter.importIso(input.getChannel(), new File(args[1]), cancel, progress);
                }
            } else {
                GameDataImporter.importZip(() -> new FileInputStream(args[0]), new File(args[1]), cancel, progress);
            }
            System.out.println("INSTALLED");
        } catch (Exception failure) { System.err.println((failure instanceof java.util.zip.ZipException ? "ZIP: " : "") + failure.getMessage()); System.exit(1); }
    }
}
