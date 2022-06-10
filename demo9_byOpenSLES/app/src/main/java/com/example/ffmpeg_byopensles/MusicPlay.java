package com.example.ffmpeg_byopensles;

public class MusicPlay {
    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    public native void play(String input);

    public native void  stop();
}
