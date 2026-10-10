package org.burstlimit.android;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.Collections;

final class LanSettings {
    static boolean validGamertag(String name) {
        if (name == null || name.isEmpty() || name.length() > 15) return false;
        for (int i = 0; i < name.length(); ++i) if (name.charAt(i) < 32 || name.charAt(i) > 126) return false;
        return !name.trim().isEmpty();
    }
    static void migrate(android.content.SharedPreferences settings) {
        if (settings.getInt("settings_version", 0) < 2) {
            // Enable discovery for existing manual-IP installs as well as new installs.
            // Keep the saved address available in the optional manual mode.
            settings.edit().putInt("settings_version", 2).putString("role", "auto").commit();
        }
    }
    static boolean validAddress(String address) {
        if (address == null) return false;
        String[] parts = address.split("\\.", -1);
        if (parts.length != 4) return false;
        int first = -1;
        for (int i = 0; i < parts.length; ++i) {
            String part = parts[i];
            if (!part.matches("[0-9]{1,3}") || (part.length() > 1 && part.startsWith("0"))) return false;
            int value = Integer.parseInt(part);
            if (value > 255) return false;
            if (i == 0) first = value;
        }
        return first > 0 && first < 224;
    }
    static boolean validRole(String role) {
        return "auto".equals(role) || "offline".equals(role) || "host".equals(role) || "join".equals(role);
    }
    static String localAddress() {
        String fallback = "";
        try {
            java.util.Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            if (interfaces == null) return "";
            for (NetworkInterface network : Collections.list(interfaces)) {
                if (!network.isUp() || network.isLoopback()) continue;
                for (InetAddress address : Collections.list(network.getInetAddresses())) {
                    if (!(address instanceof Inet4Address) || address.isLoopbackAddress() || address.isLinkLocalAddress()) continue;
                    String ip = address.getHostAddress();
                    if (!validAddress(ip)) continue;
                    if (network.getName().startsWith("wlan") || network.getName().startsWith("eth")) return ip;
                    if (fallback.isEmpty()) fallback = ip;
                }
            }
        } catch (SocketException ignored) { }
        return fallback;
    }
}
