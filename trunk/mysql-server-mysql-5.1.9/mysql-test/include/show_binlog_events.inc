--let $binlog_start=102
--replace_result $binlog_start <binlog_start>
--replace_column 2 # 4 # 5 #
--replace_regex /table_id: [0-9]+/table_id: #/
--eval show binlog events from $binlog_start
