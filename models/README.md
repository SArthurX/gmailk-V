## ONNX轉MLIR
```
model_transform.py \
    --model_name arcface \
    --model_def /workspace/ar/arcface.onnx \
    --input_shapes [[1,3,112,112]] \
    --mean 125.0,125.0,125.0 \
    --scale 0.008,0.008,0.008 \
    --keep_aspect_ratio \
    --pixel_format rgb \
    --pad_type center \
    --pad_value 114 \
    --mlir arcface.mlir
```

## MLIR轉BF16
```
model_deploy \
    --mlir arcface.mlir \
    --quantize BF16 \
    --processor cv181x \
    --model arcface_cv181x_bf16.cvimodel
```


## INT8量化
### 生成校準表
```
run_calibration arcface.mlir \    
	--dataset ../../COCO2017 \ 
	--input_num 100     	 \
	-o arcface_cali_table
```
### MLIR轉INT8
```
model_deploy \
    --mlir arcface.mlir \
    --quantize INT8 \
    --calibration_table arcface_cali_table \
    --processor cv181x \
    --model arcface_cv181x_int8_sym.cvimodel
```