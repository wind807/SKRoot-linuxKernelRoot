#!/system/bin/sh
check_reset_prop() {
  local NAME="$1"
  local EXPECTED="$2"
  local VALUE
  VALUE="$(resetprop "$NAME")"
  [ -z "$VALUE" ] || [ "$VALUE" = "$EXPECTED" ] || resetprop "$NAME" "$EXPECTED"
}


check_reset_prop "ro.product.manufacturer" "Xiaomi"
check_reset_prop "ro.product.brand" "REDMI"
check_reset_prop "ro.product.marketname" "REDMI K90"
check_reset_prop "ro.product.model" "2510DRK44C"
check_reset_prop "ro.product.name" "annibale"
