package org.burstlimit.android;

import android.os.ParcelFileDescriptor;
import java.io.File;
import java.util.concurrent.atomic.AtomicBoolean;

/** Exercises production Java extraction with Android FD ownership and sequential ZIP streams. */
public final class GameSetupInstrumentation {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    public static String runTests(android.content.Context context) throws Exception {
        File fixtures = new File(context.getExternalFilesDir(null), "setup-tests");
        File root = new File(context.getFilesDir(), "setup-import-tests-" + System.nanoTime());
        check(root.mkdir(), "Create private test output directory");
            File isoTarget = new File(root, "iso-install");
            try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(new File(fixtures, "valid.iso"), ParcelFileDescriptor.MODE_READ_ONLY);
                 ParcelFileDescriptor.AutoCloseInputStream in = new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.dup(fd.getFileDescriptor()))) {
                GameDataImporter.importIso(in.getChannel(), isoTarget, new AtomicBoolean(), (done, total, name) -> {});
            }
            check(GameDataImporter.verify(isoTarget), "ISO descriptor extraction and verification");
            File zipTarget = new File(root, "zip-install");
            GameDataImporter.importZip(() -> new ParcelFileDescriptor.AutoCloseInputStream(
                    ParcelFileDescriptor.open(new File(fixtures, "valid.zip"), ParcelFileDescriptor.MODE_READ_ONLY)),
                    zipTarget, new AtomicBoolean(), (done, total, name) -> {});
            check(GameDataImporter.verify(zipTarget), "ZIP through sequential granted descriptors");
            AtomicBoolean cancel = new AtomicBoolean(); File canceled = new File(root, "canceled");
            try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(new File(fixtures, "valid.iso"), ParcelFileDescriptor.MODE_READ_ONLY);
                 ParcelFileDescriptor.AutoCloseInputStream in = new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.dup(fd.getFileDescriptor()))) {
                try {
                    GameDataImporter.importIso(in.getChannel(), canceled, cancel, (done, total, name) -> cancel.set(true));
                    throw new AssertionError("Cancellation did not stop import");
                } catch (java.io.IOException expected) { check(expected.getMessage().contains("canceled"), "Cancellation message"); }
            }
            check(!canceled.exists(), "Canceled install not published");
            for (File file : root.listFiles()) check(!file.getName().contains(".installing-"), "Partial install cleaned up");
            File interrupted = new File(root, "retry.installing-12345");
            check(interrupted.mkdir(), "Create interrupted staging fixture");
            check(new File(interrupted, "partial.bin").createNewFile(), "Create partial file");
            GameDataImporter.cleanInterruptedSetup(new File(root, "retry"));
            check(!interrupted.exists() && GameDataImporter.verify(isoTarget), "Interrupted staging removed; complete install preserved");
            return "PASS: Android ISO/ZIP descriptors, verification, cancellation and interrupted-setup cleanup";
    }
}
