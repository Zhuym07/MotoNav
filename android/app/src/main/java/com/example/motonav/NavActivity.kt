package com.example.motonav

import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.amap.api.navi.AMapNavi
import com.amap.api.navi.AMapNaviView
import com.amap.api.navi.AMapNaviViewListener
import com.amap.api.navi.AMapNaviViewOptions
import com.amap.api.navi.enums.MapStyle
import com.amap.api.navi.model.AMapNaviLocation
import com.amap.api.navi.model.RouteOverlayOptions
import com.amap.api.navi.view.RouteOverLay
import java.lang.reflect.Proxy

/** 高德原生导航面板。MotoNav 专属控制在地图下方，不覆盖路口图和车道提示。 */
class NavActivity : AppCompatActivity() {
    companion object { const val EXTRA_TRAVEL = "navi_travel" }
    private val S = NavStyle
    private fun dp(v: Int) = S.dpi(this, v)
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var naviView: AMapNaviView
    private lateinit var root: LinearLayout
    private lateinit var muteBtn: TextView
    private var navi: AMapNavi? = null
    private var emulate = false
    private var travel = false
    private var destName = ""
    private var travelRoute: RouteOverLay? = null
    private var travelRouteKey = ""
    private var muted = false
    private var exited = false
    private var lastBack = 0L
    private var arrivedAt = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        supportActionBar?.hide()
        emulate = intent.getBooleanExtra("emu", false)
        travel = intent.getBooleanExtra(EXTRA_TRAVEL, ShareGuide.route?.isTravel == true)
        destName = intent.getStringExtra("dest") ?: ""
        muted = getSharedPreferences("motonav", Context.MODE_PRIVATE).getBoolean("nav_muted", false)
        navi = AMapNavi.getInstance(applicationContext).apply { setIsNaviTravelView(travel) }
        root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(S.BG)
        }
        naviView = AMapNaviView(this, makeOptions())
        naviView.onCreate(savedInstanceState)
        naviView.setAMapNaviViewListener(makeViewListener())
        root.addView(naviView, LinearLayout.LayoutParams(-1, 0, 1f))
        ShareGuide.route?.takeIf { !it.endpointsOnly }?.let { r ->
            naviView.map.addPolyline(com.amap.api.maps.model.PolylineOptions()
                .addAll(r.pts.map { com.amap.api.maps.model.LatLng(it[0], it[1]) })
                .width(S.dp(this, 5f)).color(Color.parseColor("#665B87EB")).zIndex(1f))
        }
        buildSuggest()
        buildControls()
        setContentView(root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }
        ViewCompat.requestApplyInsets(root)
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() { confirmBack() }
        })
        startNavi()
        handler.post(refresh)
    }

    private fun makeOptions() = AMapNaviViewOptions().apply {
        setLayoutVisible(true)
        // 骑行使用 SDK RouteOverLay，绕过拒绝骑行路径的驾车渲染器。
        setAutoDrawRoute(!travel)
        setAfterRouteAutoGray(true)
        setRouteOverlayOptions(RouteOverlayOptions().apply {
            setOnRouteCameShow(true)
            setLineWidth(S.dp(this@NavActivity, 7f))
        })
        setAutoLockCar(true)
        setScreenAlwaysBright(true)
        setLaneInfoShow(true)
        setEyrieCrossDisplay(true)
        setTrafficBarEnabled(!travel)
        setCompassEnabled(true)
        // 语音沿用 MotoNav 的设置；暂停、速度和退出由高德原生控件管理。
        setSettingMenuEnabled(false)
        setTrafficLine(!travel)
        setMapStyle(if (MapFeeder.nightNow) MapStyle.NIGHT else MapStyle.DAY, null)
    }

    private fun makeViewListener(): AMapNaviViewListener =
        Proxy.newProxyInstance(AMapNaviViewListener::class.java.classLoader, arrayOf(AMapNaviViewListener::class.java)) { proxy, m, args ->
            when (m.name) {
                "onNaviBackClick" -> { confirmBack(); return@newProxyInstance true }
                "onNaviCancel" -> exitNav()
                "hashCode" -> return@newProxyInstance System.identityHashCode(proxy)
                "equals" -> return@newProxyInstance (proxy === args?.getOrNull(0))
                "toString" -> return@newProxyInstance "MotoNavViewListener"
            }
            when (m.returnType) {
                java.lang.Boolean.TYPE -> false
                Integer.TYPE -> 0
                else -> null
            }
        } as AMapNaviViewListener

    private fun confirmBack() {
        val t = SystemClock.elapsedRealtime()
        if (t - lastBack < 2000) exitNav()
        else { lastBack = t; Toast.makeText(this, "再按一次返回退出导航", Toast.LENGTH_SHORT).show() }
    }

    private fun buildControls() {
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(8), dp(4), dp(8), dp(4))
        }
        fun button(text: String, action: () -> Unit): TextView = TextView(this).apply {
            this.text = text
            textSize = 14f
            gravity = Gravity.CENTER
            setTextColor(S.TEXT)
            background = S.pressedRound(this@NavActivity, S.CARD_SOLID, S.SELECTED_BG, 8f)
            setOnClickListener { action() }
            bar.addView(this, LinearLayout.LayoutParams(0, dp(48), 1f).apply { setMargins(dp(3), 0, dp(3), 0) })
        }
        muteBtn = button(if (muted) "开启播报" else "静音") { toggleMute() }
        root.addView(bar, LinearLayout.LayoutParams(-1, -2))
    }

    private fun startNavi() {
        try {
            val n = AMapNavi.getInstance(applicationContext)
            navi = n
            // 高德自带语音播报（静音时关掉）
            NaviVoice.load(this)                         // v3.8 按“导航语音”设置决定谁来播报
            NaviVoice.muted = muted
            NaviVoice.applyTo(n)
            ShareGuide.naviType = if (emulate) 2 else 1               // v4.5
            n.setIsNaviTravelView(travel)
            n.setMultipleRouteNaviMode(!travel)
            if (!NaviBridge.beginNavigation(emulate)) {
                Toast.makeText(this,"导航会话已结束，请返回重新规划路线",Toast.LENGTH_LONG).show()
                return
            }
            if (travel) NaviBridge.locationCallback = { updateTravelRoute(it) }
            if (n.startNavi(if (emulate) 2 else 1)) {
                if (travel) updateTravelRoute(null)
                RideRecorder.start("导航",destName,emulate)
            } else {
                NaviBridge.locationCallback = null
                NaviBridge.navigationStartFailed()
                Toast.makeText(this,"导航启动失败，请返回重新规划路线",Toast.LENGTH_LONG).show()
            }
        } catch (e: Exception) {
            NaviBridge.locationCallback = null
            NaviBridge.navigationStartFailed()
            Toast.makeText(this, "导航启动失败：${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    /** SDK route overlay uses matched step/link/point indices to erase the passed route. */
    private fun updateTravelRoute(location: AMapNaviLocation?) {
        if (isFinishing) return
        val path = navi?.naviPath ?: return
        val key = "${path.pathid}:${path.allLength}"
        if (key != travelRouteKey) {
            travelRoute?.removeFromMap()
            travelRoute?.destroy()
            travelRoute = RouteOverLay(naviView.map, path, this).apply {
                setTrafficLine(false)
                setArrowOnRoute(true)
                setPassRouteVisible(true)
                addToMap()
                showStartMarker(false)
                showRouteStart(false)
                setZindex(0)
            }
            travelRouteKey = key
        }
        if (location != null) travelRoute?.updatePolyline(location)
    }

    // ---- v4.9 更快路线提示：高德发现更快的路时弹出，点“换这条”切换（30 秒没点自动收起） ----
    private lateinit var suggestCard: LinearLayout
    private lateinit var suggestText: TextView
    private fun buildSuggest() {
        suggestCard = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            background = S.round(this@NavActivity, S.CARD_SOLID, 10f, S.PINK)
            elevation = S.dp(this@NavActivity, 6f)
            setPadding(dp(16), dp(10), dp(10), dp(10))
            visibility = View.GONE
        }
        suggestText = TextView(this).apply { textSize = 15f; setTextColor(S.TEXT); typeface = S.MEDIUM }
        suggestCard.addView(suggestText, LinearLayout.LayoutParams(0, -2, 1f))
        val yes = TextView(this).apply {
            text = "换这条"; textSize = 15f; typeface = S.BOLD; setTextColor(Color.WHITE); gravity = Gravity.CENTER
            background = S.pressedGradient(this@NavActivity, 8f); setPadding(dp(14), dp(8), dp(14), dp(8))
            setOnClickListener {
                val ok = NaviBridge.acceptSuggest()
                if (!ok) Toast.makeText(this@NavActivity, "没能切换路线", Toast.LENGTH_SHORT).show()
            }
        }
        val no = TextView(this).apply {
            text = "不用"; textSize = 15f; setTextColor(S.SUB); gravity = Gravity.CENTER; background = S.pressedRound(this@NavActivity, Color.TRANSPARENT, S.SELECTED_BG, 8f)
            setPadding(dp(12), dp(8), dp(8), dp(8))
            setOnClickListener { NaviBridge.clearSuggest() }
        }
        suggestCard.addView(yes); suggestCard.addView(no)
        root.addView(suggestCard, LinearLayout.LayoutParams(-1, -2))
    }

    private fun toggleMute() {
        muted = !muted
        getSharedPreferences("motonav", Context.MODE_PRIVATE).edit().putBoolean("nav_muted", muted).apply()
        val n = navi
        NaviVoice.muted = muted
        NaviVoice.applyTo(n)
        muteBtn.text = if (muted) "开启播报" else "静音"
        Toast.makeText(this, if (muted) "语音播报已关闭" else "语音播报已打开", Toast.LENGTH_SHORT).show()
    }

    private fun exitNav() {
        if (exited) return
        exited = true
        NaviBridge.exitNavi()
        finish()
    }

    private val refresh = object : Runnable {
        override fun run() {
            if (NaviBridge.uiSuggestMin > 0) {
                suggestText.text = "发现更快的路线，能省约 ${NaviBridge.uiSuggestMin} 分钟"
                suggestCard.visibility = View.VISIBLE
            } else suggestCard.visibility = View.GONE
            if (NaviBridge.uiArrived && !exited) {
                if (arrivedAt == 0L) {
                    arrivedAt = SystemClock.elapsedRealtime()
                    travelRoute?.onArriveDestination()
                    Toast.makeText(this@NavActivity, "已到达 $destName", Toast.LENGTH_LONG).show()
                } else if (SystemClock.elapsedRealtime() - arrivedAt > 4000) { exitNav(); return }
            }
            handler.postDelayed(this, 200)
        }
    }

    override fun onResume() { super.onResume(); naviView.onResume() }
    override fun onPause() { super.onPause(); naviView.onPause() }
    override fun onSaveInstanceState(outState: Bundle) { super.onSaveInstanceState(outState); naviView.onSaveInstanceState(outState) }
    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        if (isFinishing && !exited) { exited = true; NaviBridge.exitNavi() }
        if (travel) NaviBridge.locationCallback = null
        travelRoute?.removeFromMap()
        travelRoute?.destroy()
        travelRoute = null
        naviView.onDestroy()
        super.onDestroy()
    }
}
