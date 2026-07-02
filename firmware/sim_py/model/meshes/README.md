# 馬達 CAD 網格放這裡

把 PHU14 / PHU17 / PHU20（及連桿）的 `.stl` 或 `.glb`/`.gltf` 放入本資料夾，
再於 `../robot_config.json` 的 `meshes` 區塊填檔名，例如：

```json
"meshes": { "PHU20": "phu20.glb", "PHU17": "phu17.stl", "PHU14": "phu14.stl" }
```

`viewer3d.html` 會自動載入並取代圓柱幾何 fallback；填 `null` 則用圓柱。
網格區域座標原點請對齊關節轉軸中心，軸向沿關節 axis。
