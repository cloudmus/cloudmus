#pragma once

#include <jni.h>

// Plain JNI shared by both Android processes. The playback process runs Qt
// Core without Qt's Android bootstrap (no QtNative), so QJniObject isn't
// there to use; the UI process could, but the log goes the same way in both.
namespace Android::Jni {

// From JNI_OnLoad, on the thread that loads the library: classes are found
// with the app's class loader only there, so every class used later from a
// native thread is looked up and kept here.
void initialize(JavaVM* vm);

JavaVM* vm();
// The calling thread's env, attaching the thread on first use (detached
// when it ends).
JNIEnv* env();

// app.cloudmus.mobile.CmLog and its static methods.
jclass logClass();
jmethodID logWrite(); // (ILjava/lang/String;Ljava/lang/String;)V
jmethodID logMark(); // (Ljava/lang/String;)V

// Clears and logs a pending Java exception; true if there was one.
bool checkException(JNIEnv* env, const char* where);

} // namespace Android::Jni
