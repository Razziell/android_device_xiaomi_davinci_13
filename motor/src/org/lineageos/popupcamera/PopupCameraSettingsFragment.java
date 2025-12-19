/*
 * Copyright (C) 2020 - 2025 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.lineageos.popupcamera;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;

import androidx.preference.Preference;
import androidx.preference.Preference.OnPreferenceChangeListener;
import androidx.preference.Preference.OnPreferenceClickListener;
import androidx.preference.PreferenceFragment;

public class PopupCameraSettingsFragment extends PreferenceFragment
        implements OnPreferenceChangeListener, OnPreferenceClickListener {

    private static final String MOTOR_CALIBRATION_KEY = "motor_calibration";
    private static final String ACTION_CALIBRATE_MOTOR =
            "org.lineageos.popupcamera.action.CALIBRATE_MOTOR";

    private Preference mCalibrationPreference;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        addPreferencesFromResource(R.xml.popup_settings);

        mCalibrationPreference = findPreference(MOTOR_CALIBRATION_KEY);
        if (mCalibrationPreference != null) {
            mCalibrationPreference.setOnPreferenceClickListener(this);
        }
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        return false;
    }

    @Override
    public boolean onPreferenceClick(Preference preference) {
        if (preference != null && MOTOR_CALIBRATION_KEY.equals(preference.getKey())) {
            showCalibrationWarningDialog();
            return true;
        }
        return false;
    }

    private void showCalibrationWarningDialog() {
        final Activity a = getActivity();
        if (a == null) return;

        AlertDialog alertDialog = new AlertDialog.Builder(a)
                .setTitle(R.string.popup_calibration_warning_title)
                .setMessage(R.string.popup_calibration_warning_text)
                .setPositiveButton(R.string.popup_camera_calibrate_now, (dialog, which) -> {
                    // Never instantiate Service with 'new'. Send an explicit command via Intent.
                    Intent i = new Intent(a, PopupCameraService.class);
                    i.setAction(ACTION_CALIBRATE_MOTOR);
                    a.startService(i);
                    dialog.cancel();
                })
                .setNegativeButton(android.R.string.cancel, (dialog, which) -> dialog.cancel())
                .create();
        alertDialog.show();
    }
}
