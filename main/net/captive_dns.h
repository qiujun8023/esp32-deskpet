#pragma once

/* 启动 DNS 劫持任务,所有 A 查询都返回 AP IP,用于触发 captive portal */
void captive_dns_start(void);
