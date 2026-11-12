#include "AndroidLog.h"

#include <android/log.h>

#include "Jni.h"

namespace Logging::AndroidLog {

namespace {

int priority(char level)
{
    switch (level) {
        case 'D':
            return ANDROID_LOG_DEBUG;
        case 'W':
            return ANDROID_LOG_WARN;
        case 'E':
            return ANDROID_LOG_ERROR;
        case 'F':
            return ANDROID_LOG_FATAL;
        default:
            return ANDROID_LOG_INFO;
    }
}

jstring toJava(JNIEnv* env, const QString& text)
{
    return env->NewString(reinterpret_cast<const jchar*>(text.constData()), jsize(text.size()));
}

} // namespace

void write(char level, const QString& tag, const QString& message)
{
    JNIEnv* env = Android::Jni::env();
    if (!env || !Android::Jni::logClass()) {
        // Before CmLog is reachable: logcat at least.
        __android_log_write(priority(level), qPrintable(tag), qPrintable(message));
        return;
    }
    jstring jTag = toJava(env, tag);
    jstring jMessage = toJava(env, message);
    env->CallStaticVoidMethod(
        Android::Jni::logClass(), Android::Jni::logWrite(), jint(priority(level)), jTag, jMessage);
    Android::Jni::checkException(env, "CmLog.write");
    env->DeleteLocalRef(jTag);
    env->DeleteLocalRef(jMessage);
}

void mark(const QString& what)
{
    JNIEnv* env = Android::Jni::env();
    if (!env || !Android::Jni::logClass()) {
        __android_log_write(ANDROID_LOG_INFO, "CloudmusStart", qPrintable(what));
        return;
    }
    jstring jWhat = toJava(env, what);
    env->CallStaticVoidMethod(Android::Jni::logClass(), Android::Jni::logMark(), jWhat);
    Android::Jni::checkException(env, "CmLog.mark");
    env->DeleteLocalRef(jWhat);
}

} // namespace Logging::AndroidLog
