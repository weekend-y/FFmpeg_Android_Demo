#include <jni.h>
#include <string>
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#ifdef __cplusplus
}
#endif

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_mydemo1_1helloworld_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = avcodec_configuration();
    return env->NewStringUTF(hello.c_str());
}