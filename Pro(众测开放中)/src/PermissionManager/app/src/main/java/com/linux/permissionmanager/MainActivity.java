package com.linux.permissionmanager;

import static com.linux.permissionmanager.AppSettings.HOTLOAD_SHELL_PATH;
import static com.linux.permissionmanager.AppSettings.KEY_IS_HOTLOAD_MODE;
import static com.linux.permissionmanager.AppSettings.SHELL_SOCKET_NAME;
import static com.linux.permissionmanager.helper.MagicaRootHelper.executeMagicaRootScript;

import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;

import com.google.android.material.tabs.TabLayout;
import com.linux.permissionmanager.bridge.NativeBridge;
import com.linux.permissionmanager.fragment.SuAuthFragment;
import com.linux.permissionmanager.fragment.HomeFragment;
import com.linux.permissionmanager.fragment.SettingsFragment;
import com.linux.permissionmanager.fragment.SkrModFragment;
import com.linux.permissionmanager.helper.MagicaRootHelper;
import com.linux.permissionmanager.helper.MagicaService;
import com.linux.permissionmanager.utils.DialogUtils;
import com.linux.permissionmanager.utils.FileUtils;
import com.linux.permissionmanager.utils.GetAppListPermissionHelper;
import com.linux.permissionmanager.utils.GetSdcardPermissionsHelper;
import com.linux.permissionmanager.utils.ShellUtils;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.concurrent.CountDownLatch;

public class MainActivity extends AppCompatActivity {
    private String mRootKey = "";
    private String mHotloadCmd = "";
    private String mHotloadMethod = "SHELL";
    private String mHotloadResult = "";
    private Dialog mLoadingDialog;

    private HomeFragment mHomeFragm;
    private SuAuthFragment mSuAuthFragm;
    private SkrModFragment mSkrModFragm;
    private SettingsFragment mSettingsFragm;

    private TabLayout mBottomTab;
    private MenuItem mMainMenu;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        AppSettings.init(this);
        mRootKey = AppSettings.getString("rootKey", mRootKey);
        mHotloadCmd = AppSettings.getString("hotloadCmd", mHotloadCmd);
        mHotloadMethod = AppSettings.getString("hotloadMethod", mHotloadMethod);
        checkGetAppListPermission();
        showInputRootKeyDlg();
        setupFragment();
    }

    private void showInputRootKeyDlg() {
        boolean isHotload = AppSettings.getBoolean(KEY_IS_HOTLOAD_MODE, false);
        if (isHotload) showHotloadModeInputDlg();
        else showBootModeInputDlg();
    }

    private void showModeSelectDlg(boolean currentIsHotload, @NonNull Runnable onSelectBoot, @NonNull Runnable onSelectHotload) {
        final String[] items = {"1.刷 Boot 模式", "2.热启动模式"};
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("请选择模式");
        builder.setSingleChoiceItems(items, -1, new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                dialog.dismiss();
                if(which == 0) {
                    if (!currentIsHotload) return;
                    AppSettings.setBoolean(KEY_IS_HOTLOAD_MODE, false);
                    onSelectBoot.run();
                } else if(which == 1) {
                    if (currentIsHotload) return;
                    AppSettings.setBoolean(KEY_IS_HOTLOAD_MODE, true);
                    onSelectHotload.run();
                }
            }
        });
        builder.setNegativeButton("取消", new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) { dialog.dismiss(); }
        });
        AlertDialog dialog = builder.create();
        dialog.show();
    }

    private static class ModeRowHolder {
        LinearLayout root;
        TextView tvMode;
        TextView tvSwitch;
    }

    private ModeRowHolder createModeRow(Context context, String modeText) {
        View view = LayoutInflater.from(context).inflate(R.layout.view_mode_row, null, false);
        ModeRowHolder holder = new ModeRowHolder();
        holder.root = (LinearLayout) view;
        holder.tvMode = view.findViewById(R.id.tv_mode);
        holder.tvSwitch = view.findViewById(R.id.tv_switch);
        holder.tvMode.setText("当前模式：" + modeText);
        return holder;
    }

    private void showBootModeInputDlg() {
        int dp16 = (int) (16 * getResources().getDisplayMetrics().density);
        int dp4= (int) (4 * getResources().getDisplayMetrics().density);
        LinearLayout rootLayout = new LinearLayout(this);
        rootLayout.setOrientation(LinearLayout.VERTICAL);
        rootLayout.setPadding(dp16, dp16, dp16, dp4);
        TextView tipText = new TextView(this);
        tipText.setText("请输入 Root 权限的 Key");
        tipText.setTextSize(14);
        tipText.setPadding(0, dp4, 0, dp4);
        EditText inputTxt = new EditText(this);
        inputTxt.setText(mRootKey);
        inputTxt.setFocusable(true);
        inputTxt.setFocusableInTouchMode(true);
        inputTxt.setSelection(mRootKey.length());
        ModeRowHolder modeRow = createModeRow(this, "刷 Boot");
        rootLayout.addView(modeRow.root);
        rootLayout.addView(tipText);
        rootLayout.addView(inputTxt);
        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Root 密钥配置")
                .setIcon(android.R.drawable.ic_dialog_info)
                .setView(rootLayout)
                .setNegativeButton("取消", null)
                .setPositiveButton("确定", (d, which) -> {
                    mRootKey = inputTxt.getText().toString().trim();
                    if(TextUtils.isEmpty(mRootKey)) return;
                    AppSettings.setString("rootKey", mRootKey);
                    AppSettings.setBoolean(KEY_IS_HOTLOAD_MODE, false);
                    setupFragment();
                    switchPage(0);
                }).create();
        modeRow.tvSwitch.setOnClickListener(v -> {
            dialog.dismiss();
            showModeSelectDlg(false, this::showBootModeInputDlg, this::showHotloadModeInputDlg);
        });
        dialog.show();
    }

    private String extractConfigValue(String text, String key) {
        java.util.regex.Pattern pattern = java.util.regex.Pattern.compile("(?m)^\\s*#.*?\\b" + java.util.regex.Pattern.quote(key) + "\\s*=\\s*(\\S+)");
        java.util.regex.Matcher matcher = pattern.matcher(text);
        return matcher.find() ? matcher.group(1).trim() : "";
    }

    private void executeHotloadScript(String script, String method) {
        DialogUtils.dismissDialog(mLoadingDialog);
        mLoadingDialog = DialogUtils.showLoadingDialog(this, "正在加载热启动补丁，预计需要 1 分钟…");
        if (TextUtils.equals(method, "MAGICA")) executeMagicaRootScript(this, script, this::handleHotloadResult);
        else new Thread(() ->handleHotloadResult(ShellUtils.executeScript(this, script))).start();
    }

    private boolean isSkrootChannelOK(String rootKey) { return NativeBridge.testSkrootBasics(rootKey, "Channel").contains("OK"); }

    private void handleHotloadResult(String result) {
        runOnUiThread(() -> {
            mHotloadResult = result;
            new Handler(Looper.getMainLooper()).postDelayed(() -> {
                DialogUtils.dismissDialog(mLoadingDialog);
                mLoadingDialog = null;
                if(isSkrootChannelOK(mRootKey)) {
                    DialogUtils.showMsgDlg(this, "提示","加载完成", null);
                    setupFragment();
                    switchPage(0);
                } else DialogUtils.showLogDialog(this, mHotloadResult);
            }, 3000);
        });
    }

    private void showHotloadModeInputDlg() {
        int dp16 = (int) (16 * getResources().getDisplayMetrics().density);
        int dp4 = (int) (4 * getResources().getDisplayMetrics().density);
        LinearLayout rootLayout = new LinearLayout(this);
        rootLayout.setOrientation(LinearLayout.VERTICAL);
        rootLayout.setPadding(dp16, dp16, dp16, dp4);
        TextView tipText = new TextView(this);
        tipText.setText("可直接输入 Key，或将热启动补丁包放到 "+ HOTLOAD_SHELL_PATH +" 后点击“从1.h导入”");
        tipText.setTextSize(14);
        tipText.setPadding(0, dp4, 0, dp4);
        EditText inputTxt = new EditText(this);
        inputTxt.setText(mRootKey);
        inputTxt.setFocusable(true);
        inputTxt.setFocusableInTouchMode(true);
        inputTxt.setEnabled(TextUtils.isEmpty(mHotloadCmd));
        inputTxt.setSelection(mRootKey.length());
        ModeRowHolder modeRow = createModeRow(this, "热启动");
        rootLayout.addView(modeRow.root);
        rootLayout.addView(tipText);
        rootLayout.addView(inputTxt);
        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Root 密钥配置")
                .setIcon(android.R.drawable.ic_dialog_info)
                .setView(rootLayout)
                .setNegativeButton("取消", null)
                .setNeutralButton("从1.h导入", null)
                .setPositiveButton("确定", (d, which) -> {
                    mRootKey = inputTxt.getText().toString().trim();
                    if(TextUtils.isEmpty(mRootKey)) return;
                    AppSettings.setString("rootKey", mRootKey);
                    AppSettings.setBoolean(KEY_IS_HOTLOAD_MODE, true);
                    setupFragment();
                    switchPage(0);
                    if(!isSkrootChannelOK(mRootKey) && !TextUtils.isEmpty(mHotloadCmd)) {
                        executeHotloadScript(mHotloadCmd, mHotloadMethod);
                    }
                }).create();
        modeRow.tvSwitch.setOnClickListener(v -> {
            dialog.dismiss();
            showModeSelectDlg(true, this::showBootModeInputDlg, this::showHotloadModeInputDlg);
        });
        dialog.show();
        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
            if(!GetSdcardPermissionsHelper.getPermissions(this, this, this.getPackageName())) {
                DialogUtils.showNeedPermissionDialog(this);
                return;
            }
            String scriptText = FileUtils.readTextFile(HOTLOAD_SHELL_PATH);
            if (TextUtils.isEmpty(scriptText)) DialogUtils.showMsgDlg(this, "读取文件失败或文件不存在", "错误", null);
            String rootKey = extractConfigValue(scriptText, "ROOT_KEY");
            String method = extractConfigValue(scriptText, "METHOD");
            if (TextUtils.isEmpty(rootKey)) return;
            mRootKey = rootKey;
            mHotloadMethod = method;
            mHotloadCmd = scriptText;
            inputTxt.setText(mRootKey);
            inputTxt.setEnabled(false);
            AppSettings.setString("rootKey", mRootKey);
            AppSettings.setString("hotloadCmd", mHotloadCmd);
            AppSettings.setString("hotloadMethod", mHotloadMethod);
        });
    }
    
    private void checkGetAppListPermission() {
        if(GetAppListPermissionHelper.getPermissions(this)) return;
        DialogUtils.showCustomDialog(this,"权限申请","请授予读取APP列表权限，再重新打开",null,"确定",
                (dialog, which) -> {
                    dialog.dismiss();
                    finish();
                },null, null);
    }

    private void setupFragment() {
        mBottomTab = findViewById(R.id.bottom_tab);
        mBottomTab.removeAllTabs();
        mBottomTab.addTab(mBottomTab.newTab().setText("主页"), true);
        mBottomTab.addTab(mBottomTab.newTab().setText("授权"));
        mBottomTab.addTab(mBottomTab.newTab().setText("模块"));
        mBottomTab.addTab(mBottomTab.newTab().setText("设置"));
        mHomeFragm = new HomeFragment(MainActivity.this, mRootKey);
        mSuAuthFragm = new SuAuthFragment(MainActivity.this, mRootKey);
        mSkrModFragm = new SkrModFragment(MainActivity.this, mRootKey);
        mSettingsFragm = new SettingsFragment(MainActivity.this, mRootKey);
        switchPage(0);
        mBottomTab.addOnTabSelectedListener(new TabLayout.OnTabSelectedListener() {
            @Override public void onTabSelected(TabLayout.Tab tab) { switchPage(tab.getPosition()); }
            @Override public void onTabUnselected(TabLayout.Tab tab) {}
            @Override public void onTabReselected(TabLayout.Tab tab) {}
        });
    }

    private void switchPage(int index) {
        Fragment f;
        switch (index) {
            case 0: f = mHomeFragm; break;
            case 1: f = mSuAuthFragm; break;
            case 2: f = mSkrModFragm; break;
            default: f = mSettingsFragm; break;
        }
        if(f == null) return;
        getSupportFragmentManager()
                .beginTransaction()
                .replace(R.id.frame_layout, f)
                .commit();
        if(mMainMenu != null) mMainMenu.setVisible(index == 1 || index == 2);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_main, menu);
        mMainMenu = menu.findItem(R.id.action_add);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == R.id.action_add) {
            View anchor = findViewById(R.id.action_add);
            showMainPopupMenu(anchor);
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        onChooseFileActivityResult(requestCode, resultCode, data);
    }

    private void showMainPopupMenu(View v) {
        int index = mBottomTab.getSelectedTabPosition();
        if(index == 1) mSuAuthFragm.showSuAuthMainPopupMenu(v);
        if(index == 2)  mSkrModFragm.showSkrModMainPopupMenu(v);
    }

    private void onChooseFileActivityResult(int requestCode, int resultCode, Intent data) {
        mSkrModFragm.onChooseFileActivityResult(requestCode, resultCode, data);
    }

}