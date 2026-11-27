package app.cloudmus.mobile;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

// An HTTP proxy on this device that passes everything on through a source's
// proxy, for the sign-in browser: a WebView can't give a SOCKS5 proxy a
// password. Only CONNECT, which is all HTTPS needs; host names go to the
// proxy to resolve, as for the sources (docs/protocol.md §4.3). It listens on
// the loopback address only, and only while it's open. No Android API, so it
// runs on any JVM.
final class ProxyTunnel implements Closeable {
    private static final int TIMEOUT_MS = 20000;
    private static final int MAX_HEAD = 16 * 1024;

    private final boolean socks5;
    private final String proxyHost;
    private final int proxyPort;
    private final String username;
    private final String password;
    private final ServerSocket server;
    private final Set<Socket> open = Collections.synchronizedSet(new HashSet<>());

    ProxyTunnel(boolean socks5, String proxyHost, int proxyPort, String username, String password)
            throws IOException {
        this.socks5 = socks5;
        this.proxyHost = proxyHost;
        this.proxyPort = proxyPort;
        this.username = username;
        this.password = password;
        server = new ServerSocket(0, 50, InetAddress.getByName("127.0.0.1"));
        start(() -> {
            while (!server.isClosed()) {
                try {
                    Socket client = server.accept();
                    open.add(client);
                    start(() -> serve(client));
                } catch (IOException closed) {
                    break;
                }
            }
        });
    }

    int port() {
        return server.getLocalPort();
    }

    @Override
    public void close() {
        closeQuietly(server);
        synchronized (open) {
            for (Socket socket : open)
                closeQuietly(socket);
            open.clear();
        }
    }

    private static void start(Runnable task) {
        Thread thread = new Thread(task, "proxy-tunnel");
        thread.setDaemon(true);
        thread.start();
    }

    private void serve(Socket client) {
        Socket upstream = new Socket();
        open.add(upstream);
        try {
            client.setSoTimeout(TIMEOUT_MS);
            String[] request = readHead(client.getInputStream()).split("\r\n")[0].split(" ");
            if (request.length < 2 || !"CONNECT".equals(request[0])) {
                reply(client, "405 Method Not Allowed");
                return;
            }
            int colon = request[1].lastIndexOf(':');
            String host = request[1].substring(0, colon).replace("[", "").replace("]", "");
            int port = Integer.parseInt(request[1].substring(colon + 1));
            try {
                upstream.connect(new InetSocketAddress(proxyHost, proxyPort), TIMEOUT_MS);
                upstream.setSoTimeout(TIMEOUT_MS);
                if (socks5)
                    openSocks(upstream, host, port);
                else
                    openHttp(upstream, request[1]);
            } catch (IOException refused) {
                reply(client, "502 Bad Gateway");
                return;
            }
            reply(client, "200 Connection established");
            // A page may keep a connection idle for long.
            client.setSoTimeout(0);
            upstream.setSoTimeout(0);
            start(() -> pipe(upstream, client));
            pipe(client, upstream);
        } catch (IOException | RuntimeException gone) {
            // A malformed request, or the browser went away.
        } finally {
            drop(client);
            drop(upstream);
        }
    }

    private void drop(Socket socket) {
        closeQuietly(socket);
        open.remove(socket);
    }

    private void openHttp(Socket proxy, String target) throws IOException {
        StringBuilder request = new StringBuilder("CONNECT ").append(target).append(" HTTP/1.1\r\nHost: ")
                .append(target).append("\r\n");
        if (!username.isEmpty()) {
            byte[] credentials = (username + ":" + password).getBytes(StandardCharsets.UTF_8);
            request.append("Proxy-Authorization: Basic ").append(java.util.Base64.getEncoder().encodeToString(credentials)).append("\r\n");
        }
        request.append("\r\n");
        proxy.getOutputStream().write(request.toString().getBytes(StandardCharsets.ISO_8859_1));
        String[] status = readHead(proxy.getInputStream()).split("\r\n")[0].split(" ");
        if (status.length < 2 || !"200".equals(status[1]))
            throw new IOException("the proxy refused");
    }

    // RFC 1928, with RFC 1929's user name and password.
    private void openSocks(Socket proxy, String host, int port) throws IOException {
        InputStream in = proxy.getInputStream();
        OutputStream out = proxy.getOutputStream();
        boolean auth = !username.isEmpty();
        out.write(auth ? new byte[] {5, 1, 2} : new byte[] {5, 1, 0});
        byte[] chosen = read(in, 2);
        if (chosen[0] != 5 || chosen[1] != (auth ? 2 : 0))
            throw new IOException("the proxy offers no usable way in");
        if (auth) {
            byte[] user = username.getBytes(StandardCharsets.UTF_8);
            byte[] secret = password.getBytes(StandardCharsets.UTF_8);
            out.write(new byte[] {1, (byte) user.length});
            out.write(user);
            out.write(secret.length);
            out.write(secret);
            if (read(in, 2)[1] != 0)
                throw new IOException("the proxy refused the password");
        }
        byte[] name = host.getBytes(StandardCharsets.UTF_8);
        byte[] connect = new byte[7 + name.length];
        connect[0] = 5;
        connect[1] = 1;
        connect[3] = 3; // a domain name
        connect[4] = (byte) name.length;
        System.arraycopy(name, 0, connect, 5, name.length);
        connect[5 + name.length] = (byte) (port >> 8);
        connect[6 + name.length] = (byte) port;
        out.write(connect);
        byte[] answer = read(in, 4);
        if (answer[0] != 5 || answer[1] != 0)
            throw new IOException("the proxy couldn't connect: " + answer[1]);
        // The address it bound, of no interest: IPv4, a name or IPv6, and a port.
        int length = answer[3] == 1 ? 4 : answer[3] == 4 ? 16 : read(in, 1)[0] & 0xff;
        read(in, length + 2);
    }

    private static byte[] read(InputStream in, int length) throws IOException {
        byte[] bytes = new byte[length];
        int done = 0;
        while (done < length) {
            int got = in.read(bytes, done, length - done);
            if (got < 0)
                throw new IOException("closed early");
            done += got;
        }
        return bytes;
    }

    // Up to the empty line, a byte at a time: what follows isn't the head's.
    private static String readHead(InputStream in) throws IOException {
        StringBuilder head = new StringBuilder();
        while (head.length() < MAX_HEAD) {
            int next = in.read();
            if (next < 0)
                throw new IOException("closed early");
            head.append((char) next);
            int length = head.length();
            if (next == '\n' && length >= 4 && "\r\n\r\n".contentEquals(head.subSequence(length - 4, length)))
                return head.toString();
        }
        throw new IOException("head too long");
    }

    private static void reply(Socket client, String status) throws IOException {
        client.getOutputStream().write(("HTTP/1.1 " + status + "\r\n\r\n").getBytes(StandardCharsets.ISO_8859_1));
    }

    private static void pipe(Socket from, Socket to) {
        try {
            byte[] buffer = new byte[32 * 1024];
            InputStream in = from.getInputStream();
            OutputStream out = to.getOutputStream();
            int got;
            while ((got = in.read(buffer)) >= 0)
                out.write(buffer, 0, got);
        } catch (IOException gone) {
            // Either side closed.
        } finally {
            closeQuietly(from);
            closeQuietly(to);
        }
    }

    private static void closeQuietly(Closeable closeable) {
        try {
            closeable.close();
        } catch (IOException ignored) {
        }
    }

}
