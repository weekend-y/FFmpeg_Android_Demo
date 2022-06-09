package com.example.ffmpeg_audiodecode;

import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("native-lib");
    }

    private final static String TAG = "Weekend";
    private Button mBtnPlay;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        checkPermission();

        mBtnPlay = (Button) findViewById(R.id.btnStartVideo);

        mBtnPlay.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        String PATH = Environment.getExternalStorageDirectory().getPath();
                        //视频解码
                        String input = PATH + File.separator + "input.mp3";
                        String output = PATH + File.separator + "audioDecodeOut.pcm";
                        decode_audio(input,output);
                        Toast.makeText(MainActivity.this, "音频解码完成，请自行从手机中拉取pcm文件", Toast.LENGTH_SHORT).show();
                    }
                });
            }
        });
    }

    public Boolean checkPermission() {
        boolean isGranted = true;
        if (android.os.Build.VERSION.SDK_INT >= 23) {
            if (this.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                isGranted = false;
            }
            if (this.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                isGranted = false;
            }
            Log.i("读写权限获取"," ： "+isGranted);
            if (!isGranted) {
                this.requestPermissions(
                        new String[]{Manifest.permission.ACCESS_COARSE_LOCATION, Manifest.permission
                                .ACCESS_FINE_LOCATION,
                                Manifest.permission.READ_EXTERNAL_STORAGE,
                                Manifest.permission.WRITE_EXTERNAL_STORAGE},
                        102);
            }
        }
        return isGranted;
    }


    public native void decode_audio(String inputStr,String outputStr);
}