package org.burstlimit.android;

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.*;

/** ISO/ZIP setup without game execution, native libraries or broad storage access. */
public final class GameDataImporter {
    private static final long XEX_SIZE = 7983104, MAX_BYTES = 16L << 30;
    private static final String USA_SHA1 = "aec598f88cf51181fc377b148e0b1ad30db4485c";
    private static final String[] ARCHIVES = {"LONG2DATA_US.CPK", "STREAM_US.CPK", "STREAM_JP.CPK"};
    public interface Progress { void update(long done, long total, String file); }
    private static String lower(String s) { return s.toLowerCase(Locale.ROOT); }
    private static IOException error(String s) { return new IOException(s); }
    private static void canceled(AtomicBoolean cancel) throws IOException {
        if (cancel.get()) throw error("Setup canceled. Choose your game file to try again.");
    }
    private static String sha1(byte[] bytes) throws IOException {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-1").digest(bytes);
            StringBuilder result = new StringBuilder();
            for (byte b : digest) result.append(String.format(Locale.ROOT, "%02x", b & 255));
            return result.toString();
        } catch (java.security.NoSuchAlgorithmException e) { throw new IOException(e); }
    }
    private static byte[] readXex(InputStream input, AtomicBoolean cancel) throws IOException {
        byte[] data = new byte[(int) XEX_SIZE];
        int done = 0;
        while (done < data.length) {
            canceled(cancel);
            int n = input.read(data, done, data.length - done);
            if (n <= 0) throw error("Couldn't verify default.xex. The download may be incomplete.");
            done += n;
        }
        return data;
    }
    public static boolean verify(File root) {
        try {
            File xex = new File(root, "default.xex");
            if (xex.length() != XEX_SIZE) return false;
            try (InputStream in = new FileInputStream(xex)) {
                if (!USA_SHA1.equals(sha1(readXex(in, new AtomicBoolean())))) return false;
            }
            for (String name : ARCHIVES) {
                File file = new File(root, "LONG2DATA/" + name);
                if (!file.isFile() || file.length() == 0) return false;
            }
            return true;
        } catch (IOException e) { return false; }
    }
    static synchronized void cleanInterruptedSetup(File target) {
        File[] siblings = target.getParentFile().listFiles();
        if (siblings == null) return;
        for (File sibling : siblings)
            if (sibling.isDirectory() && sibling.getName().matches(java.util.regex.Pattern.quote(target.getName() + ".installing-") + "[0-9]+"))
                remove(sibling);
    }
    private static String safe(String name) throws IOException {
        if (name.endsWith("/")) name = name.substring(0, name.length() - 1);
        if (name.isEmpty() || name.length() > 4096 || name.startsWith("/"))
            throw error("The game file contains an invalid filename.");
        for (char c : name.toCharArray())
            if (c < 32 || c == 127 || c == '\\' || c == ':') throw error("The game file contains an invalid filename.");
        for (String part : name.split("/", -1))
            if (part.isEmpty() || part.equals(".") || part.equals("..")) throw error("The game file contains an unsafe filename.");
        return name;
    }
    private static String canonical(String name) {
        if (lower(name).equals("default.xex")) return "default.xex";
        for (String archive : ARCHIVES)
            if (lower(name).equals(lower("LONG2DATA/" + archive))) return "LONG2DATA/" + archive;
        return name;
    }
    private static final class Item {
        final String path, source; final long offset, size;
        Item(String path, String source, long offset, long size) {
            this.path = canonical(path); this.source = source; this.offset = offset; this.size = size;
        }
    }
    private static long validate(List<Item> files, byte[] xex) throws IOException {
        if (xex == null || xex.length != XEX_SIZE || !USA_SHA1.equals(sha1(xex)))
            throw error("This isn't the supported Dragon Ball Z: Burst Limit USA Xbox 360 version. Choose the USA game file.");
        Set<String> names = new HashSet<>(); long total = 0;
        for (Item file : files) {
            if (!names.add(lower(file.path))) throw error("The game file contains duplicate filenames.");
            if (file.size < 0 || file.size > MAX_BYTES - total) throw error("The game file contains too much data.");
            total += file.size;
        }
        for (String name : ARCHIVES) {
            boolean found = false;
            for (Item file : files) if (file.path.equals("LONG2DATA/" + name) && file.size > 0) found = true;
            if (!found) throw error("The game file is missing a required Burst Limit archive.");
        }
        return total;
    }
    private static File staging(File target, long total) throws IOException {
        Files.createDirectories(target.getParentFile().toPath());
        if (target.exists() && (!target.isDirectory() || Objects.requireNonNull(target.list()).length != 0))
            throw error("The game installation folder already contains files. Existing files were kept.");
        if (target.getParentFile().getUsableSpace() < total + (64L << 20))
            throw error("Not enough free space. Game setup needs about 3.4 GB.");
        return Files.createTempDirectory(target.getParentFile().toPath(), target.getName() + ".installing-").toFile();
    }
    private static void remove(File root) {
        File[] children = root.listFiles();
        if (children != null) for (File child : children) remove(child);
        root.delete();
    }
    private static OutputStream output(File root, String name) throws IOException {
        File file = new File(root, safe(name));
        Files.createDirectories(file.getParentFile().toPath());
        return new BufferedOutputStream(new FileOutputStream(file), 1 << 20);
    }
    private static void publish(File stage, File target, AtomicBoolean cancel) throws IOException {
        canceled(cancel);
        if (!verify(stage)) throw error("Game verification failed. Please try setup again.");
        canceled(cancel);
        if (target.exists()) {
            if (!target.isDirectory() || Objects.requireNonNull(target.list()).length != 0 || !target.delete())
                throw error("The installation folder changed during setup. Existing files were kept.");
        }
        // Sibling directories reside on the same filesystem; never publish partial files.
        if (!stage.renameTo(target)) throw error("Couldn't finish installing the game files.");
    }

    private static final class Iso {
        final FileChannel channel; final AtomicBoolean cancel; final long size;
        long base; final List<Item> files = new ArrayList<>();
        final Set<String> directories = new HashSet<>(), names = new HashSet<>();
        Iso(FileChannel channel, AtomicBoolean cancel) throws IOException {
            this.channel = channel; this.cancel = cancel;
            try { size = channel.size(); } catch (IOException e) {
                throw error("This file provider cannot read an ISO directly. Download the ISO to this device and choose it from Downloads.");
            }
            for (long candidate : new long[] {0, 0xFB20, 0x20600, 0x2080000, 0xFD90000}) {
                long at = candidate + 32 * 2048;
                if (at > size || size - at < 28) continue;
                ByteBuffer header = read(at, 28);
                byte[] magic = new byte[20]; header.get(magic);
                if (!new String(magic, StandardCharsets.US_ASCII).equals("MICROSOFT*XBOX*MEDIA")) continue;
                base = candidate;
                directory(Integer.toUnsignedLong(header.getInt()), Integer.toUnsignedLong(header.getInt()), "", 0);
                return;
            }
            throw error("This isn't an Xbox 360 ISO. Choose a finished .iso or the extracted game ZIP.");
        }
        ByteBuffer read(long offset, int length) throws IOException {
            canceled(cancel);
            if (offset < 0 || offset > size || length < 0 || length > size - offset || length > (32 << 20))
                throw error("The ISO is incomplete or has an invalid file entry.");
            ByteBuffer buffer = ByteBuffer.allocate(length).order(ByteOrder.LITTLE_ENDIAN);
            while (buffer.hasRemaining()) {
                canceled(cancel);
                int n = channel.read(buffer, offset + buffer.position());
                if (n <= 0) throw error("Couldn't read the ISO. Check that the download has finished.");
            }
            buffer.flip(); return buffer;
        }
        void directory(long sector, long length, String parent, int depth) throws IOException {
            if (depth > 64 || length < 14 || length > (32 << 20) || !directories.add(sector + ":" + length))
                throw error("The ISO contains an invalid directory table.");
            ByteBuffer table = read(base + sector * 2048, (int) length);
            Deque<Integer> pending = new ArrayDeque<>(); pending.add(0);
            Set<Integer> visited = new HashSet<>();
            while (!pending.isEmpty()) {
                canceled(cancel);
                int node = pending.removeLast(), at = node * 4;
                if (!visited.add(node) || at + 14 > table.limit()) throw error("The ISO contains an invalid directory tree.");
                int left = Short.toUnsignedInt(table.getShort(at)), right = Short.toUnsignedInt(table.getShort(at + 2));
                long childSector = Integer.toUnsignedLong(table.getInt(at + 4)), bytes = Integer.toUnsignedLong(table.getInt(at + 8));
                int attrs = table.get(at + 12) & 255, n = table.get(at + 13) & 255;
                if (n == 0 || at + 14 + n > table.limit()) throw error("The ISO contains an invalid filename.");
                byte[] encoded = new byte[n]; table.position(at + 14); table.get(encoded);
                String name = safe(new String(encoded, StandardCharsets.ISO_8859_1));
                if (name.contains("/")) throw error("The ISO contains an unsafe filename.");
                String path = parent + name;
                if (names.size() >= 20000 || !names.add(lower(path))) throw error("The ISO contains duplicate or too many filenames.");
                if (left != 0) pending.add(left); if (right != 0) pending.add(right);
                if (parent.isEmpty() && lower(name).equals("$systemupdate")) continue;
                if ((attrs & 0x10) != 0) { if (bytes > 0) directory(childSector, bytes, path + "/", depth + 1); }
                else {
                    long start = base + childSector * 2048;
                    if (start > size || bytes > size - start) throw error("The ISO is incomplete or has an invalid file entry.");
                    files.add(new Item(path, path, start, bytes));
                }
            }
        }
    }
    public static synchronized void importIso(FileChannel channel, File target, AtomicBoolean cancel, Progress progress) throws IOException {
        canceled(cancel); progress.update(0, 0, "Checking USA game version…");
        Iso iso = new Iso(channel, cancel); Item xex = null;
        for (Item file : iso.files) if (file.path.equals("default.xex")) xex = file;
        if (xex == null || xex.size != XEX_SIZE) throw error("This ISO doesn't contain the supported USA Xbox 360 game.");
        long total = validate(iso.files, iso.read(xex.offset, (int) xex.size).array());
        File stage = staging(target, total);
        try {
            iso.files.sort(Comparator.comparingLong(f -> f.offset)); long done = 0;
            for (Item file : iso.files) {
                canceled(cancel);
                try (OutputStream out = output(stage, file.path)) {
                    for (long copied = 0; copied < file.size;) {
                        byte[] bytes = iso.read(file.offset + copied, (int) Math.min(4 << 20, file.size - copied)).array();
                        out.write(bytes); copied += bytes.length; done += bytes.length;
                        progress.update(done, total, file.path);
                    }
                }
            }
            publish(stage, target, cancel);
        } finally { remove(stage); }
    }
    public interface Source { InputStream open() throws IOException; }
    public static synchronized void importZip(Source source, File target, AtomicBoolean cancel, Progress progress) throws IOException {
        canceled(cancel); progress.update(0, 0, "Checking ZIP and USA game version…");
        List<Item> original = new ArrayList<>(); Set<String> names = new HashSet<>();
        Map<String, Long> sizes = new HashMap<>();
        byte[] xex = null, buffer = new byte[1 << 20]; String prefix = ""; long scanned = 0;
        // File-provider grants belong to the descriptor, not a reopenable path.
        // Two sequential passes avoid copying a multi-GB source into app cache.
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(source.open(), 1 << 20))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                canceled(cancel); String name = safe(entry.getName());
                if (names.size() >= 20000 || !names.add(lower(name))) throw error("The ZIP contains duplicate or too many filenames.");
                boolean executable = !entry.isDirectory() && lower(name.substring(name.lastIndexOf('/') + 1)).equals("default.xex");
                ByteArrayOutputStream data = executable ? new ByteArrayOutputStream((int) XEX_SIZE) : null;
                if (executable && (xex != null || (entry.getSize() >= 0 && entry.getSize() != XEX_SIZE)))
                    throw error("This ZIP doesn't contain just the supported USA Xbox 360 game.");
                long bytes = 0; int n;
                while ((n = zip.read(buffer)) != -1) {
                    canceled(cancel);
                    if (n > MAX_BYTES - scanned || (executable && n > XEX_SIZE - bytes))
                        throw error("The ZIP contains an invalid file size or too much data.");
                    if (data != null) data.write(buffer, 0, n);
                    bytes += n; scanned += n;
                    progress.update(0, 0, "Checking ZIP…\n" + name);
                }
                if (entry.isDirectory() && bytes != 0) throw error("The ZIP contains an invalid directory.");
                sizes.put(entry.getName(), bytes);
                if (!entry.isDirectory()) original.add(new Item(name, entry.getName(), 0, bytes));
                if (executable) {
                    if (bytes != XEX_SIZE) throw error("This ZIP doesn't contain the supported USA Xbox 360 game.");
                    xex = data.toByteArray();
                    if (!USA_SHA1.equals(sha1(xex))) throw error("This isn't the supported Dragon Ball Z: Burst Limit USA Xbox 360 version. Choose the USA game file.");
                    prefix = name.substring(0, name.lastIndexOf('/') + 1);
                }
            }
        } catch (EOFException e) { throw error("Couldn't read the ZIP. It may be incomplete or damaged."); }
        if (xex == null) throw error("The ZIP has no default.xex. If it contains an ISO, unzip it and choose that ISO.");
        List<Item> files = new ArrayList<>(); Map<String, Item> selected = new HashMap<>();
        for (Item entry : original) {
            if (!entry.source.startsWith(prefix)) continue;
            String relative = entry.source.substring(prefix.length());
            if (lower(relative).startsWith("$systemupdate/")) continue;
            Item file = new Item(safe(relative), entry.source, 0, entry.size);
            files.add(file); selected.put(file.source, file);
        }
        long total = validate(files, xex); File stage = staging(target, total);
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(source.open(), 1 << 20))) {
            long done = 0; Set<String> seen = new HashSet<>(); ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                canceled(cancel); safe(entry.getName());
                Long expected = sizes.get(entry.getName());
                if (expected == null || !seen.add(entry.getName())) throw error("The ZIP changed during setup. Please choose it again.");
                Item file = selected.get(entry.getName()); long copied = 0;
                try (OutputStream out = file == null ? null : output(stage, file.path)) {
                    int n;
                    while ((n = zip.read(buffer)) != -1) {
                        canceled(cancel);
                        if (n > expected - copied) throw error("The ZIP is incomplete or changed during setup.");
                        copied += n;
                        if (out != null) { out.write(buffer, 0, n); done += n; progress.update(done, total, file.path); }
                    }
                }
                if (copied != expected) throw error("The ZIP is incomplete or changed during setup.");
            }
            if (seen.size() != sizes.size()) throw error("The ZIP changed during setup. Some game files are missing.");
            publish(stage, target, cancel);
        } catch (EOFException e) { throw error("Couldn't read the ZIP. It may be incomplete or damaged."); }
        finally { remove(stage); }
    }
}
