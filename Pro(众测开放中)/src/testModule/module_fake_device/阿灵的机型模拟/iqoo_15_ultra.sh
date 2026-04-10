#!/system/bin/sh
check_reset_prop() {
  local NAME="$1"
  local EXPECTED="$2"
  local VALUE
  VALUE="$(resetprop "$NAME")"
  [ -z "$VALUE" ] || [ "$VALUE" = "$EXPECTED" ] || resetprop "$NAME" "$EXPECTED"
}


check_reset_prop "ro.product.manufacturer" "vivo"
check_reset_prop "ro.product.brand" "vivo"
check_reset_prop "ro.product.marketname" "iQOO 15 Ultra"
check_reset_prop "ro.product.model" "V2546A"
