package com.example.motonav

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.cos

class RideHistoryActivity : AppCompatActivity() {
    private val S = NavStyle
    private lateinit var items: LinearLayout
    private lateinit var status: TextView
    private val handler = Handler(Looper.getMainLooper())
    private var exportId: String? = null
    private var generation = 0
    private val exportFile = registerForActivityResult(ActivityResultContracts.CreateDocument("application/gpx+xml")) { uri ->
        val id = exportId; exportId=null
        if (uri != null && id != null) {
            toast("正在导出 GPX…")
            RideRecorder.export(id,uri) { error -> if (!isDestroyed) toast(if (error==null) "GPX 已导出" else "导出失败：$error") }
        }
    }
    private val refresh = object : Runnable {
        override fun run() { status.text=RideRecorder.status(); handler.postDelayed(this,1000) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        exportId=savedInstanceState?.getString("export_id")
        window.statusBarColor = S.BG
        window.navigationBarColor = S.BG
        @Suppress("DEPRECATION")
        if (Build.VERSION.SDK_INT >= 26) window.decorView.systemUiVisibility =
            window.decorView.systemUiVisibility or View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
        @Suppress("DEPRECATION")
        if (Build.VERSION.SDK_INT >= 27) window.decorView.systemUiVisibility =
            window.decorView.systemUiVisibility or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        supportActionBar?.hide()

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(S.BG)
        }
        root.setOnApplyWindowInsetsListener { v, ins ->
            @Suppress("DEPRECATION")
            val top=if(Build.VERSION.SDK_INT>=30) ins.getInsets(WindowInsets.Type.systemBars()).top else ins.systemWindowInsetTop
            @Suppress("DEPRECATION")
            val bottom=if(Build.VERSION.SDK_INT>=30) ins.getInsets(WindowInsets.Type.systemBars()).bottom else ins.systemWindowInsetBottom
            v.setPadding(0,top,0,bottom)
            ins
        }
        root.addView(header())

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(12), dp(12), dp(24))
        }
        val scroll = ScrollView(this).apply {
            setBackgroundColor(S.BG)
            isFillViewport = true
            clipToPadding = true
            addView(content)
        }
        root.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))

        content.addView(settingsCard())
        content.addView(secondaryButton("刷新记录") { load() },
            LinearLayout.LayoutParams(-1, dp(48)).apply { topMargin = dp(12) })
        content.addView(sectionHeader(), LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(18) })

        items = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        content.addView(items)
        setContentView(root)
    }

    override fun onResume() { super.onResume(); handler.post(refresh); load() }
    override fun onPause() { handler.removeCallbacks(refresh); super.onPause() }
    override fun onDestroy() { generation++; handler.removeCallbacksAndMessages(null); super.onDestroy() }
    override fun onSaveInstanceState(outState: Bundle) { outState.putString("export_id",exportId); super.onSaveInstanceState(outState) }

    private fun header() = FrameLayout(this).apply {
        setBackgroundColor(S.CARD_SOLID)
        addView(TextView(this@RideHistoryActivity).apply {
            text = "‹ 返回"
            textSize = 14f
            typeface = S.MEDIUM
            setTextColor(S.CYAN)
            gravity = Gravity.CENTER
            includeFontPadding = false
            background = S.pressedRound(this@RideHistoryActivity, Color.TRANSPARENT, S.SELECTED_BG, 8f)
            setOnClickListener { finish() }
        }, FrameLayout.LayoutParams(dp(64), dp(40), Gravity.START or Gravity.CENTER_VERTICAL).apply {
            leftMargin = dp(4)
        })
        addView(TextView(this@RideHistoryActivity).apply {
            text = "骑行轨迹"
            textSize = 16f
            typeface = S.BOLD
            setTextColor(S.TITLE)
            gravity = Gravity.CENTER
            includeFontPadding = false
        }, FrameLayout.LayoutParams(-1, dp(48), Gravity.CENTER))
        addView(View(this@RideHistoryActivity).apply { setBackgroundColor(S.DIVIDER) },
            FrameLayout.LayoutParams(-1, dp(1), Gravity.BOTTOM))
    }

    private fun settingsCard() = card().apply {
        addView(label("记录设置", 16f, S.TEXT, bold = true))
        addView(label("自动保存真实导航与巡航", 12f, S.SUB).apply {
            setPadding(0, dp(2), 0, dp(10))
        })
        val row = LinearLayout(this@RideHistoryActivity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            minimumHeight = dp(60)
        }
        val copy = LinearLayout(this@RideHistoryActivity).apply {
            orientation = LinearLayout.VERTICAL
        }
        copy.addView(label("自动记录", 15f, S.TEXT))
        copy.addView(label("真实导航与巡航；模拟导航不记录", 12f, S.SUB).apply {
            setPadding(0, 0, 0, dp(6))
        })
        row.addView(copy, LinearLayout.LayoutParams(0, -2, 1f))
        row.addView(toggle({ RideRecorder.enabled }) { value ->
            RideRecorder.setEnabled(value)
            status.text = RideRecorder.status()
        })
        addView(row)
        addView(divider())
        status = label(RideRecorder.status(), 13f, S.CYAN, bold = true).apply {
            setPadding(0, dp(4), 0, dp(2))
        }
        addView(status)
        addView(label("轨迹保存在本机，不主动上传。卸载会清除记录，重要轨迹请导出 GPX 保存。", 12f, S.SUB).apply {
            setLineSpacing(dp(2).toFloat(), 1f)
            setPadding(0, dp(4), 0, 0)
        })
    }

    private fun sectionHeader() = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(0, dp(2), 0, dp(2))
        addView(label("历史轨迹", 16f, S.TEXT, bold = true), LinearLayout.LayoutParams(0, -2, 1f))
        addView(label("本机记录", 12f, S.SUB))
    }

    private fun load() {
        val token=++generation
        RideRecorder.history { all,error ->
            if (isDestroyed || token!=generation) return@history
            items.removeAllViews()
            if (error!=null) {
                items.addView(label("读取失败：$error", 14f, S.SUB).apply { setPadding(0, dp(16), 0, 0) })
                return@history
            }
            if (all.isEmpty()) { items.addView(emptyState()); return@history }
            items.addView(label("共 ${all.size} 条记录${if(all.size>100) "，显示最近 100 条" else ""}", 12f, S.SUB).apply {
                setPadding(0, dp(8), 0, dp(2))
            })
            for (s in all.take(100)) {
                val box=LinearLayout(this).apply {
                    orientation=LinearLayout.VERTICAL
                    setPadding(dp(14),dp(12),dp(14),dp(12))
                    background=S.pressedRound(this@RideHistoryActivity,S.CARD_SOLID,S.SELECTED_BG,10f,S.STROKE)
                    elevation=dp(1).toFloat()
                    isClickable=true
                    isFocusable=true
                }
                val titleRow = LinearLayout(this).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.CENTER_VERTICAL
                }
                titleRow.addView(label(s.title, 16f, S.TEXT, bold = true).apply { maxLines=2 }, LinearLayout.LayoutParams(0, -2, 1f))
                titleRow.addView(TextView(this).apply {
                    text = "›"
                    textSize = 22f
                    setTextColor(S.HINT)
                    gravity = Gravity.CENTER
                    includeFontPadding = false
                }, LinearLayout.LayoutParams(dp(20), dp(28)))
                box.addView(titleRow)
                val date=SimpleDateFormat("yyyy-MM-dd HH:mm",Locale.CHINA).format(Date(s.start))
                box.addView(label("$date · ${s.mode}${if(s.active) " · 记录中" else if(s.interrupted) " · 中断后已恢复" else ""}", 12f, S.SUB).apply {
                    setPadding(0, 0, 0, dp(6))
                })
                val metrics = LinearLayout(this).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.CENTER_VERTICAL
                    setPadding(0, dp(2), 0, dp(2))
                }
                metrics.addView(label(RideRecorder.distanceText(s.meters), 17f, S.CYAN, bold = true))
                metrics.addView(label("·", 14f, S.HINT).apply { setPadding(dp(8), 0, dp(8), 0) })
                metrics.addView(label(RideRecorder.durationText(s.end-s.start), 17f, S.CYAN, bold = true))
                box.addView(metrics)
                box.addView(label("最高 ${s.maxKmh.toInt()} km/h · ${s.points} 个点", 12f, S.SUB).apply {
                    setPadding(0, dp(3), 0, 0)
                })
                box.setOnClickListener { detail(s) }
                items.addView(box,LinearLayout.LayoutParams(-1,-2).apply { topMargin=dp(10) })
            }
        }
    }

    private fun emptyState() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        gravity = Gravity.CENTER
        setPadding(dp(24), dp(38), dp(24), dp(38))
        addView(TextView(this@RideHistoryActivity).apply {
            text = "◎"
            textSize = 38f
            setTextColor(S.HINT)
            gravity = Gravity.CENTER
            includeFontPadding = false
        })
        addView(label("还没有轨迹", 16f, S.SUB, bold = true).apply {
            gravity = Gravity.CENTER
            setPadding(0, dp(8), 0, 0)
        })
        addView(label("开始真实导航或巡航，收到有效定位后会自动记录。", 12f, S.HINT).apply {
            gravity = Gravity.CENTER
            setPadding(0, dp(3), 0, 0)
        })
    }

    private fun detail(s: RideSummary) {
        val box=LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL
            setPadding(dp(12),0,dp(12),dp(4))
        }
        box.addView(TextView(this).apply {
            text="轨迹示意（无底图）\n蓝色为轨迹，绿色为起点，红色为终点。定位中断处不连线。"
            textSize=12f
            setTextColor(S.SUB)
            setLineSpacing(dp(2).toFloat(), 1f)
            setPadding(0,dp(8),0,dp(8))
        })
        val preview=RidePreview(this).apply {
            background = S.round(this@RideHistoryActivity, S.CARD2, 10f, S.STROKE)
            clipToOutline = true
        }
        box.addView(preview,LinearLayout.LayoutParams(-1,dp(260)).apply { topMargin=dp(2) })
        val dialog=AlertDialog.Builder(this,R.style.Theme_MotoNav_Dialog)
            .setTitle(s.title).setView(box)
            .setPositiveButton("导出 GPX") { _,_ ->
                exportId=s.id
                val date=SimpleDateFormat("yyyyMMdd-HHmmss",Locale.US).format(Date(s.start))
                exportFile.launch("MotoNav-$date.gpx")
            }.setNegativeButton("关闭",null)
            .setNeutralButton("删除") { _,_ ->
                AlertDialog.Builder(this,R.style.Theme_MotoNav_Dialog)
                    .setTitle("删除这条轨迹？").setMessage("删除后无法恢复。")
                    .setPositiveButton("删除") { _,_ -> RideRecorder.delete(s.id) { err -> if(!isDestroyed) { if(err!=null) toast(err); load() } } }
                    .setNegativeButton("取消",null).show()
            }.show()
        RideRecorder.preview(s.id) { points,error ->
            if (!isDestroyed && dialog.isShowing) { preview.points=points; preview.invalidate(); if(error!=null) toast("轨迹读取失败：$error") }
        }
    }

    private fun card() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(12), dp(12), dp(12), dp(12))
        background = S.round(this@RideHistoryActivity, S.CARD_SOLID, 10f, S.STROKE)
        elevation = dp(1).toFloat()
    }

    private fun divider() = View(this).apply {
        setBackgroundColor(S.DIVIDER)
        layoutParams = LinearLayout.LayoutParams(-1, dp(1)).apply {
            topMargin = dp(8)
            bottomMargin = dp(2)
        }
    }

    private fun secondaryButton(text: String, click: () -> Unit) = TextView(this).apply {
        this.text = text
        textSize = 15f
        typeface = S.MEDIUM
        setTextColor(S.TEXT)
        gravity = Gravity.CENTER
        includeFontPadding = false
        background = S.pressedRound(this@RideHistoryActivity, S.CARD_SOLID, S.CARD2, 10f, S.STROKE)
        setOnClickListener { click() }
    }

    private fun toggle(get: () -> Boolean, onChange: (Boolean) -> Unit): View {
        val track = FrameLayout(this)
        val indicator = View(this).apply { background = S.oval(this@RideHistoryActivity, S.SUB) }
        track.addView(indicator, FrameLayout.LayoutParams(dp(6), dp(6)).apply { topMargin = dp(9) })
        val knob = View(this).apply {
            background = S.oval(this@RideHistoryActivity, S.CARD_SOLID, S.STROKE)
            elevation = dp(1).toFloat()
        }
        track.addView(knob, FrameLayout.LayoutParams(dp(20), dp(20)).apply { topMargin = dp(2) })
        track.background = S.pressedRound(this, S.CARD_SOLID, S.CARD2, 12f, S.STROKE)
        track.layoutParams = LinearLayout.LayoutParams(dp(52), dp(24)).apply { leftMargin = dp(12) }
        track.isClickable = true
        track.contentDescription = "自动记录真实导航和巡航"
        fun paint() {
            val on = get()
            (indicator.background as? GradientDrawable)?.setColor(if (on) S.PINK else S.HINT)
            (indicator.layoutParams as FrameLayout.LayoutParams).leftMargin = if (on) dp(9) else dp(37)
            (knob.layoutParams as FrameLayout.LayoutParams).leftMargin = if (on) dp(30) else dp(2)
            indicator.requestLayout()
            knob.requestLayout()
        }
        track.setOnClickListener { onChange(!get()); paint() }
        paint()
        return track
    }

    private fun dp(n: Int)=(n*resources.displayMetrics.density+0.5f).toInt()
    private fun label(s: String,size: Float,color: Int,bold: Boolean=false)=TextView(this).apply {
        text=s
        textSize=size
        setTextColor(color)
        includeFontPadding=false
        if (bold) typeface=S.BOLD
        setPadding(0,dp(6),0,dp(6))
    }
    private fun toast(s: String)=Toast.makeText(this,s,Toast.LENGTH_LONG).show()
}

private class RidePreview(c: Context): View(c) {
    var points: List<RidePoint> = emptyList()
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG)
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(NavStyle.CARD2)
        if (points.isEmpty()) {
            paint.style=Paint.Style.FILL
            paint.color=NavStyle.HINT
            paint.textSize=14*resources.displayMetrics.scaledDensity
            paint.textAlign=Paint.Align.CENTER
            paint.typeface=NavStyle.MEDIUM
            canvas.drawText("等待轨迹数据…",width/2f,height/2f,paint)
            return
        }
        val lat0=points.map { it.lat }.average()
        val scaleX=cos(Math.toRadians(lat0)).coerceAtLeast(0.01)
        val minX=points.minOf { it.lng*scaleX }; val maxX=points.maxOf { it.lng*scaleX }
        val minY=points.minOf { it.lat }; val maxY=points.maxOf { it.lat }
        val pad=24*resources.displayMetrics.density
        val scale=minOf((width-2*pad)/(maxX-minX).coerceAtLeast(0.0001),(height-2*pad)/(maxY-minY).coerceAtLeast(0.0001)).coerceAtLeast(0.0)
        fun x(p: RidePoint)=(width/2.0+(p.lng*scaleX-(minX+maxX)/2)*scale).toFloat()
        fun y(p: RidePoint)=(height/2.0-(p.lat-(minY+maxY)/2)*scale).toFloat()
        val path=Path()
        points.forEachIndexed { i,p -> if(i==0 || p.segment) path.moveTo(x(p),y(p)) else path.lineTo(x(p),y(p)) }
        paint.style=Paint.Style.STROKE
        paint.strokeCap=Paint.Cap.ROUND
        paint.strokeJoin=Paint.Join.ROUND
        paint.strokeWidth=5*resources.displayMetrics.density
        paint.color=Color.argb(42,91,135,235)
        canvas.drawPath(path,paint)
        paint.strokeWidth=2.5f*resources.displayMetrics.density
        paint.color=NavStyle.PINK
        canvas.drawPath(path,paint)
        endpoint(canvas,x(points.first()),y(points.first()),NavStyle.GREEN,5f)
        endpoint(canvas,x(points.last()),y(points.last()),NavStyle.RED,4.5f)
    }

    private fun endpoint(canvas: Canvas, x: Float, y: Float, color: Int, radiusDp: Float) {
        val density=resources.displayMetrics.density
        paint.style=Paint.Style.FILL
        paint.color=Color.WHITE
        canvas.drawCircle(x,y,(radiusDp+2f)*density,paint)
        paint.color=color
        canvas.drawCircle(x,y,radiusDp*density,paint)
    }
}