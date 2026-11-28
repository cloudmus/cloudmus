package app.cloudmus.mobile;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

// This device's addresses that another device on a local network may reach,
// the likeliest first. A head unit has a dozen interfaces, most of them its
// own insides (the car's network, a modem), and which one a phone is on
// isn't known: a link offers them all, or one at a time, best first.
//
//   1. a Wi-Fi network this device is on (the phone's hotspot, say);
//   2. a hotspot of its own (interfaces named so: wlan, ap, swlan, softap);
//   3. the rest.
//
// Site-local IPv4 only: what a phone's browser can open by number.
final class LocalAddresses {
    private static final String TAG = "LocalAddresses";

    private LocalAddresses() {
    }

    static final class Address {
        final String host;
        final String face;

        Address(String host, String face) {
            this.host = host;
            this.face = face;
        }

        @Override
        public String toString() {
            return host + " (" + face + ")";
        }
    }

    static List<Address> find(Context context) {
        Set<String> wifiClient = wifiClientAddresses(context);
        List<Address> client = new ArrayList<>();
        List<Address> hotspot = new ArrayList<>();
        List<Address> others = new ArrayList<>();
        try {
            for (NetworkInterface face : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!face.isUp() || face.isLoopback())
                    continue;
                String name = face.getName();
                for (InetAddress address : Collections.list(face.getInetAddresses())) {
                    if (!(address instanceof Inet4Address) || !address.isSiteLocalAddress())
                        continue;
                    Address found = new Address(address.getHostAddress(), name);
                    if (wifiClient.contains(found.host))
                        client.add(found);
                    else if (wifiLike(name))
                        hotspot.add(found);
                    else
                        others.add(found);
                }
            }
        } catch (Exception error) {
            CmLog.e(TAG, "interfaces", error);
        }
        client.addAll(hotspot);
        client.addAll(others);
        return client;
    }

    private static boolean wifiLike(String name) {
        return name.startsWith("wlan") || name.startsWith("ap") || name.startsWith("swlan")
                || name.startsWith("softap");
    }

    // This device's addresses on the Wi-Fi networks it's a client of.
    private static Set<String> wifiClientAddresses(Context context) {
        Set<String> found = new HashSet<>();
        try {
            ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
            for (Network network : connectivity.getAllNetworks()) {
                NetworkCapabilities capabilities = connectivity.getNetworkCapabilities(network);
                LinkProperties link = connectivity.getLinkProperties(network);
                if (capabilities == null || link == null
                        || !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI))
                    continue;
                for (LinkAddress address : link.getLinkAddresses()) {
                    if (address.getAddress() instanceof Inet4Address)
                        found.add(address.getAddress().getHostAddress());
                }
            }
        } catch (RuntimeException error) {
            CmLog.e(TAG, "networks", error);
        }
        return found;
    }

    // The network of this device's that `host` is on: the one with an
    // address in the same subnet. null with none, and the system picks.
    static Network networkFor(Context context, InetAddress host) {
        try {
            ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
            for (Network network : connectivity.getAllNetworks()) {
                LinkProperties link = connectivity.getLinkProperties(network);
                if (link == null)
                    continue;
                for (LinkAddress address : link.getLinkAddresses()) {
                    if (sameSubnet(address.getAddress(), host, address.getPrefixLength()))
                        return network;
                }
            }
        } catch (RuntimeException error) {
            CmLog.e(TAG, "networks", error);
        }
        return null;
    }

    private static boolean sameSubnet(InetAddress a, InetAddress b, int prefix) {
        byte[] x = a.getAddress();
        byte[] y = b.getAddress();
        if (x.length != y.length || prefix <= 0)
            return false;
        for (int bit = 0; bit < prefix && bit < x.length * 8; ++bit) {
            int mask = 0x80 >> (bit % 8);
            if ((x[bit / 8] & mask) != (y[bit / 8] & mask))
                return false;
        }
        return true;
    }
}
