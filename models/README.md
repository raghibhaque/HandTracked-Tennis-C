# Models directory

Drop model files here to enable the DNN palm detector and the face-cascade
fallback mask. Both are optional — if neither file is present, the tracker
falls back to the pure color pipeline.

## Required for Tier 1 DNN mode: `palm.onnx`

A YOLOv8-nano ONNX model trained on hand detection. Single class, standard
Ultralytics export.

**Recommended source (community-trained, single class "hand"):**
- https://github.com/cansik/yolo-hand-detection — pretrained weights, exportable to ONNX.
- Or export your own from Ultralytics:
  ```bash
  pip install ultralytics
  yolo export model=yolov8n-hand.pt format=onnx imgsz=640 opset=12
  ```

**Expected file:** `models/palm.onnx`

**Expected output tensor:** shape `(1, 5, 8400)` or `(1, 8400, 5)` — decoder
handles both. Columns: `[cx, cy, w, h, obj_conf]`.

**Override path:** set env var `HAND_TENNIS_MODEL` to any absolute path.

Model missing → tracker prints:
```
DNN palm model not found — using color fallback pipeline.
```
and behavior is identical to the previous color-only version.

## Optional Tier 2 fallback aid: `haarcascade_frontalface_default.xml`

Used ONLY when the color pipeline is active (i.e. no palm.onnx). Detected
face rectangles are subtracted from the skin mask so faces don't compete
with the hand.

Ships with every OpenCV install at:
```
<opencv_install>/etc/haarcascades/haarcascade_frontalface_default.xml
```
Copy that file to `models/haarcascade_frontalface_default.xml`.

**Override path:** set env var `HAND_TENNIS_HAAR`.

Missing → face masking silently disabled (color pipeline still works, just
loses to a face in view).

## Runtime controls

- `R` key in game — force full recalibration (re-run background sampling
  and re-learn adaptive skin bounds). Keep hand OUT of view for ~1.5 s.
