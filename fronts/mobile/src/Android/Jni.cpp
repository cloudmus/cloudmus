#include "Jni.h"

#include <android/log.h>
#include <pthread.h>

namespace Android::Jni {

namespace {

JavaVM* javaVm = nullptr;
jclass cmLog = nullptr;
jmethodID cmLogWrite = nullptr;
jmethodID cmLogMark = nullptr;
pthread_key_t detachKey;

void detachThread(void*)
{
    if (javaVm)
        javaVm->DetachCurrentThread();
}

} // namespace

void initialize(JavaVM* vm)
{
    javaVm = vm;
    pthread_key_create(&detachKey, detachThread);
    JNIEnv* e = env();
    jclass local = e->FindClass("dev/cloudmus/mobile/CmLog");
    if (checkException(e, "FindClass CmLog") || !local)
        return;
    cmLog = static_cast<jclass>(e->NewGlobalRef(local));
    e->DeleteLocalRef(local);
    cmLogWrite = e->GetStaticMethodID(cmLog, "write", "(ILjava/lang/String;Ljava/lang/String;)V");
    cmLogMark = e->GetStaticMethodID(cmLog, "mark", "(Ljava/lang/String;)V");
    checkException(e, "CmLog methods");
}

JavaVM* vm() { return javaVm; }

JNIEnv* env()
{
    if (!javaVm)
        return nullptr;
    JNIEnv* e = nullptr;
    if (javaVm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK)
        return e;
    if (javaVm->AttachCurrentThread(&e, nullptr) != JNI_OK)
        return nullptr;
    pthread_setspecific(detachKey, e);
    return e;
}

jclass logClass() { return cmLog; }

jmethodID logWrite() { return cmLogWrite; }

jmethodID logMark() { return cmLogMark; }

bool checkException(JNIEnv* e, const char* where)
{
    if (!e->ExceptionCheck())
        return false;
    e->ExceptionDescribe();
    e->ExceptionClear();
    __android_log_print(ANDROID_LOG_ERROR, "cloudmus", "JNI exception in %s", where);
    return true;
}

} // namespace Android::Jni
