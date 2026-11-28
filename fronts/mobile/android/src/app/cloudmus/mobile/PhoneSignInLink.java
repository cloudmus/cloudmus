package app.cloudmus.mobile;

import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.SecureRandom;
import java.util.Base64;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

// What the two devices of a sign-in by phone share: the key that goes from
// one to the other in the QR code, never over the network, and the way what
// they send each other is sealed with it (AES-256-GCM: unreadable, and only
// the key's holder can have written it). No Android API.
final class PhoneSignInLink {
    private static final int IV = 12;
    private static final int TAG_BITS = 128;

    private PhoneSignInLink() {
    }

    static String newKey() {
        byte[] key = new byte[32];
        new SecureRandom().nextBytes(key);
        return Base64.getUrlEncoder().withoutPadding().encodeToString(key);
    }

    // `direction` keeps one side's message from passing for the other's.
    static byte[] seal(String key, String direction, String text) throws GeneralSecurityException {
        byte[] iv = new byte[IV];
        new SecureRandom().nextBytes(iv);
        byte[] sealed = cipher(Cipher.ENCRYPT_MODE, key, direction, iv)
                .doFinal(text.getBytes(StandardCharsets.UTF_8));
        byte[] message = new byte[IV + sealed.length];
        System.arraycopy(iv, 0, message, 0, IV);
        System.arraycopy(sealed, 0, message, IV, sealed.length);
        return message;
    }

    static String open(String key, String direction, byte[] message) throws GeneralSecurityException {
        if (message.length < IV)
            throw new GeneralSecurityException("too short");
        byte[] iv = new byte[IV];
        System.arraycopy(message, 0, iv, 0, IV);
        byte[] text = cipher(Cipher.DECRYPT_MODE, key, direction, iv).doFinal(message, IV, message.length - IV);
        return new String(text, StandardCharsets.UTF_8);
    }

    private static Cipher cipher(int mode, String key, String direction, byte[] iv) throws GeneralSecurityException {
        byte[] raw;
        try {
            raw = Base64.getUrlDecoder().decode(key);
        } catch (IllegalArgumentException malformed) {
            throw new GeneralSecurityException("not a key");
        }
        if (raw.length != 32)
            throw new GeneralSecurityException("not a key");
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(mode, new SecretKeySpec(raw, "AES"), new GCMParameterSpec(TAG_BITS, iv));
        cipher.updateAAD(direction.getBytes(StandardCharsets.UTF_8));
        return cipher;
    }
}
