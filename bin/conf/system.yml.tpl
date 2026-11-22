# InkStackSearchServer 配置模板（由 switch-env.sh / collect.sh 渲染）
#
# 独立搜索进程：监听 RPC Hub，同时提供搜索服务。
# 只保留代码实际读取的配置项。

servers:
  - address: ["0.0.0.0:${RPC_PORT}"]
    keepalive: 1
    timeout: 5000
    name: chen-search-hub/1.0.0
    accept_worker: accept
    io_worker: io
    process_worker: io
    type: rpc
    args:
      relay: "1"
      relay_timeout: ${RELAY_TIMEOUT_MS}

fiber:
  stack_size: 65536

workers:
  io:
    thread_num: ${IO_THREADS}
  accept:
    thread_num: ${ACCEPT_THREADS}

logs:
  - name: root
    level: ${LOG_LEVEL}
    appenders:
      - type: FileLogAppender
        file: ${LOG_DIR}/root.log
      - type: StdoutLogAppender
  - name: search
    level: ${LOG_LEVEL}
    appenders:
      - type: FileLogAppender
        file: ${LOG_DIR}/search.log
      - type: StdoutLogAppender

search:
  # 索引文件落盘路径（父目录需可写）
  index_path: ${INDEX_PATH}
  # cppjieba 词典目录
  jieba_dict_path: ${DICT_PATH}
  # true: 启动时丢弃索引文件，等待 InkStackServer 全量快照重建
  rebuild_on_start: ${REBUILD_ON_START}
  save_interval_ms: ${SAVE_INTERVAL_MS}
  rebuild_timeout_ms: ${REBUILD_TIMEOUT_MS}
  max_page_size: ${MAX_PAGE_SIZE}

server:
  work_path: ${WORK_PATH}
  data_path: ${DATA_PATH}
  pid_file: ${PID_FILE}
  instance_id: ${INSTANCE_ID}
