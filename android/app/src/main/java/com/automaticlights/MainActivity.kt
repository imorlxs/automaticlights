package com.automaticlights

import android.Manifest
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.automaticlights.databinding.ActivityMainBinding
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.io.IOException

class MainActivity : AppCompatActivity() {

    companion object {
        private const val BLE_UUID          = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
        private const val RSSI_THRESHOLD    = -75        // dBm — lower = needs to be closer
        private const val BEACON_TIMEOUT_MS = 10_000L   // absent if not seen for 10 s
        private const val POLL_INTERVAL_MS  = 3_000L    // status refresh interval
        private const val PREFS_NAME        = "autolights"
    }

    private lateinit var binding: ActivityMainBinding
    private lateinit var prefs: SharedPreferences

    private val btAdapter by lazy {
        (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val http = OkHttpClient()
    private val handler = Handler(Looper.getMainLooper())

    private var userPresent = false
    private var lastBeaconMs = 0L
    private var scanning = false

    // ── BLE scan callback ─────────────────────────────────────────────

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (result.rssi >= RSSI_THRESHOLD) {
                lastBeaconMs = System.currentTimeMillis()
                if (!userPresent) {
                    userPresent = true
                    reportUser(detected = true, rssi = result.rssi)
                }
                runOnUiThread {
                    binding.tvBleStatus.text = "Beacon: DETECTED  (RSSI ${result.rssi} dBm)"
                }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            runOnUiThread {
                binding.tvBleStatus.text = "BLE scan error (code $errorCode)"
            }
        }
    }

    // ── Periodic tasks ────────────────────────────────────────────────

    // Marks user absent if beacon not seen within BEACON_TIMEOUT_MS
    private val absenceChecker = object : Runnable {
        override fun run() {
            val elapsed = System.currentTimeMillis() - lastBeaconMs
            if (userPresent && elapsed > BEACON_TIMEOUT_MS) {
                userPresent = false
                reportUser(detected = false, rssi = 0)
                runOnUiThread {
                    binding.tvBleStatus.text = "Beacon: out of range"
                }
            }
            handler.postDelayed(this, 2_000)
        }
    }

    private val statusPoller = object : Runnable {
        override fun run() {
            fetchStatus()
            handler.postDelayed(this, POLL_INTERVAL_MS)
        }
    }

    // ── Lifecycle ─────────────────────────────────────────────────────

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        binding.etName.setText(prefs.getString("user_name", ""))
        binding.etServer.setText(prefs.getString("server_url", "http://192.168.1.189:8080"))

        binding.btnSave.setOnClickListener     { saveConfig() }
        binding.btnLightOn.setOnClickListener  { sendLightCmd(on = true) }
        binding.btnLightOff.setOnClickListener { sendLightCmd(on = false) }

        requestBlePermissions()
    }

    override fun onResume() {
        super.onResume()
        startScanning()
        handler.post(absenceChecker)
        handler.post(statusPoller)
    }

    override fun onPause() {
        super.onPause()
        stopScanning()
        handler.removeCallbacks(absenceChecker)
        handler.removeCallbacks(statusPoller)
    }

    // ── BLE ───────────────────────────────────────────────────────────

    private fun startScanning() {
        if (scanning || !hasBlePermission()) return
        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid.fromString(BLE_UUID))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        btAdapter.bluetoothLeScanner?.startScan(listOf(filter), settings, scanCallback)
        scanning = true
        binding.tvBleStatus.text = "Scanning for beacon…"
    }

    private fun stopScanning() {
        if (!scanning || !hasBlePermission()) return
        btAdapter.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
    }

    // ── API calls ─────────────────────────────────────────────────────

    private fun serverUrl() = prefs.getString("server_url", "http://192.168.1.189:8080")!!

    private fun reportUser(detected: Boolean, rssi: Int) {
        val name = prefs.getString("user_name", "")?.trim() ?: return
        if (name.isBlank()) return

        val body = """{"user":"$name","detected":$detected,"rssi":$rssi}"""
            .toRequestBody("application/json".toMediaType())
        val req = Request.Builder()
            .url("${serverUrl()}/api/user/detected")
            .post(body)
            .build()
        http.newCall(req).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {}
            override fun onResponse(call: Call, response: Response) { response.close() }
        })
    }

    private fun fetchStatus() {
        val req = Request.Builder().url("${serverUrl()}/api/state").build()
        http.newCall(req).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                runOnUiThread { binding.tvLight.text = "Light: server unreachable" }
            }
            override fun onResponse(call: Call, response: Response) {
                val text = response.body?.string() ?: return response.close()
                response.close()
                runOnUiThread { updateStatusUi(JSONObject(text)) }
            }
        })
    }

    private fun sendLightCmd(on: Boolean) {
        val req = Request.Builder()
            .url("${serverUrl()}/api/light/${if (on) "on" else "off"}")
            .post("".toRequestBody())
            .build()
        http.newCall(req).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                runOnUiThread { toast("Request failed") }
            }
            override fun onResponse(call: Call, response: Response) {
                val msg = if (response.isSuccessful) {
                    "Light turned ${if (on) "ON" else "OFF"}"
                } else {
                    runCatching { JSONObject(response.body?.string() ?: "") }
                        .getOrNull()?.optString("detail") ?: "Error ${response.code}"
                }
                response.close()
                runOnUiThread { toast(msg) }
                fetchStatus()
            }
        })
    }

    // ── UI ────────────────────────────────────────────────────────────

    private fun updateStatusUi(j: JSONObject) {
        binding.tvLight.text    = "Light: ${j.optString("light", "?")}"
        binding.tvDoor.text     = "Door: ${j.optString("door", "?")}"
        binding.tvPresence.text = "Presence: ${j.optString("presence", "?")}"
        binding.tvN.text        = "Users in room: ${j.optInt("N", 0)}"
        binding.tvLux.text      = "Lux: ${"%.1f".format(j.optDouble("lux", 0.0))}"
        binding.tvLastUser.text = "Last user: ${j.optString("lastUser", "—")}"
    }

    private fun saveConfig() {
        prefs.edit()
            .putString("user_name",  binding.etName.text.toString().trim())
            .putString("server_url", binding.etServer.text.toString().trim())
            .apply()
        toast("Saved")
    }

    private fun toast(msg: String) =
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    // ── Permissions ───────────────────────────────────────────────────

    private val permLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        if (results.values.all { it }) startScanning()
        else toast("BLE permissions required for beacon detection")
    }

    private fun requestBlePermissions() {
        val needed = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = needed.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isEmpty()) startScanning() else permLauncher.launch(missing.toTypedArray())
    }

    private fun hasBlePermission(): Boolean {
        val perm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            Manifest.permission.BLUETOOTH_SCAN
        else
            Manifest.permission.ACCESS_FINE_LOCATION
        return ContextCompat.checkSelfPermission(this, perm) == PackageManager.PERMISSION_GRANTED
    }
}
