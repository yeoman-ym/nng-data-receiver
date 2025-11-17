#!/bin/bash

PORT=12121
ACTION=$1
TIMEOUT=$2

case $ACTION in
    "block")
        echo "[$(date)] 阻断 nanomsg 端口 $PORT"
        iptables -A INPUT -p tcp --dport $PORT -j DROP
        iptables -A OUTPUT -p tcp --dport $PORT -j DROP
        
        if [ ! -z "$TIMEOUT" ] && [ "$TIMEOUT" -gt 0 ]; then
            echo "将在 $TIMEOUT 秒后自动恢复"
            nohup bash -c "sleep $TIMEOUT && $0 unblock" > /dev/null 2>&1 &
        fi
        ;;
        
    "unblock")
        echo "[$(date)] 恢复 nanomsg 端口 $PORT"
        iptables -D INPUT -p tcp --dport $PORT -j DROP 2>/dev/null || true
        iptables -D OUTPUT -p tcp --dport $PORT -j DROP 2>/dev/null || true
        ;;
        
    "status")
        echo "当前端口 $PORT 状态:"
        iptables -L -n | grep $PORT || echo "端口未阻断"
        ;;
        
    *)
        echo "用法: $0 {block|unblock|status} [timeout_seconds]"
        echo "示例:"
        echo "  $0 block          # 永久阻断"
        echo "  $0 block 30       # 阻断30秒后自动恢复"
        echo "  $0 unblock        # 恢复端口"
        echo "  $0 status         # 查看状态"
        ;;
esac
