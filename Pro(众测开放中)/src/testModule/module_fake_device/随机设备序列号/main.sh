#!/system/bin/sh
DIGITS="0123456789"
LOWERCASE="abcdefghijklmnopqrstuvwxyz"
UPPERCASE="ABCDEFGHIJKLMNOPQRSTUVWXYZ"

get_random_char() {
    local type="$1"
    local current="$2"
    local result=""
    
    while [ "$result" = "" ]; do
        local new_char=""
        local pos=0
        
        case "$type" in
            "digit")
                pos=$(($RANDOM % 10))
                new_char=$(echo "$DIGITS" | cut -c$((pos+1)))
                ;;
            "lower")
                pos=$(($RANDOM % 26))
                new_char=$(echo "$LOWERCASE" | cut -c$((pos+1)))
                ;;
            "upper")
                pos=$(($RANDOM % 26))
                new_char=$(echo "$UPPERCASE" | cut -c$((pos+1)))
                ;;
        esac
        
        if [ "$new_char" != "$current" ]; then
            result="$new_char"
        fi
    done
    echo "$result"
}

generate_new_sn() {
    local original_sn="$1"
    local new_sn=""
    local length=${#original_sn}
    
    local i=0
    while [ $i -lt $length ]; do
        local char="${original_sn:$i:1}"
        
        if echo "$char" | grep -q '^[0-9]$'; then
            new_sn="${new_sn}$(get_random_char "digit" "$char")"
        elif echo "$char" | grep -q '^[a-z]$'; then
            new_sn="${new_sn}$(get_random_char "lower" "$char")"
        elif echo "$char" | grep -q '^[A-Z]$'; then
            new_sn="${new_sn}$(get_random_char "upper" "$char")"
        else
            new_sn="${new_sn}${char}"
        fi
        
        i=$((i + 1))
    done
    echo "$new_sn"
}

clear
echo -e "正在获取当前序列号..."

CURRENT_ID=$(resetprop ro.serialno 2>/dev/null)

if [ -z "$CURRENT_ID" ]; then
    echo -e "错误：无法获取当前序列号！"
    exit 1
fi

echo -e "正在生成随机序列号..."

NEW_NAME=$(generate_new_sn "$CURRENT_ID")

sleep 1
resetprop ro.serialno "$NEW_NAME"

sleep 0.5
VERIFY_ID=$(resetprop ro.serialno 2>/dev/null)

if [ "$VERIFY_ID" = "$NEW_NAME" ]; then
    echo -e "成功！"
    echo -e "原序列号: $CURRENT_ID"
    echo -e "新序列号: $VERIFY_ID"
else
    echo -e "警告：设置后验证不一致。"
    echo -e "当前值: $VERIFY_ID"
fi
