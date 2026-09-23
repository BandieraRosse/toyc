# RFANIM V1 文本格式

> 状态：当前
> 所有者：动作文本格式与验证规则

公开动作位于 `rasterfall/assets/actions/`。文本格式以 `RFANIM 1` 开头，依次声明 `action`、
`layer`、毫秒 `duration`、`skeleton RF_HUMANOID_V1`、`loop`，每个 `track` 使用小写 stable humanoid role
和 `step|linear`，`key` 为 `time-ms rotation-x rotation-y rotation-z`。解析器拒绝未知动作、role、
插值、逆序/越界关键帧和超出固定容量的数据。`layer` 使用 LOWER_BODY、UPPER_BODY 或 ADDITIVE；
源格式骨名只能在离线导入时解析，不得写入 RFANIM。当前文本样例见
`rasterfall/assets/actions/rifle_idle.rfanim`，可用 `build/rf_anim_info` 检查解析结果。
