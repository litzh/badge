# 来源

本目录复用同工作区 RLCD 项目的 `firmware/rlcd/src/esp_codec_dev` Arduino 版本，
复制时 RLCD HEAD 为 `6b156cc3374df6b93cbe7d2d32bd5fd5b6b0c496`。
保留原有 LICENSE、版权声明及组件文档，移除编辑器设置和组件管理器校验缓存，
清理源文件及文档的行尾空格和文档混合缩进；未更改驱动逻辑。

随附 `idf_component.yml` 标识上游组件为 Espressif esp_codec_dev 1.3.5，
上游仓库 `https://github.com/espressif/esp-adf`，组件目录 `components/esp_codec_dev`，
对应提交 `9b35bca1a6db3d989936f228d6e28f33089fa9e7`。

该目录按 Arduino sketch 的 `src/` 规则参与构建，不通过 IDF 组件管理器安装。
badge 当前只调用 ES7210 输入，未启用 ES8311 扬声器播放。
