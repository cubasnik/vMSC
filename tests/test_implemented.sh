#!/bin/bash
# ============================================================================
# vMSC Master Test Suite — All 24/24 Message Generators
# ============================================================================
# Consolidated test file combining:
#   - test_implemented.sh: Core 24 tests
#   - test_generators.sh: Parameter validation tests
#   - test_all_24.sh: Alternative test cases
#   - run_tests.sh: Infrastructure (kept if needed)
# ============================================================================

BIN="./build/vmsc"
PASS=0
FAIL=0
TESTS=()

# ============================================================================
# Utility Functions
# ============================================================================

run_test() {
    local num=$1
    local name=$2
    local cmd=$3
    local expected=$4
    
    echo -n "$num. $name... "
    # Get both the "raw" line and the next line(s)
    OUTPUT=$($BIN $cmd --no-color 2>&1 | grep -i "raw" -A 2 | tr '\n' ' ' || true)
    
    if echo "$OUTPUT" | grep -qi "$expected"; then
        echo "✓"
        ((PASS++))
        TESTS+=("✓ $num: $name")
    else
        echo "✗"
        ((FAIL++))
        TESTS+=("✗ $num: $name")
    fi
}

# Parameter test helper
test_param() {
    local num=$1
    local name=$2
    local cmd=$3
    local param=$4
    local value=$5
    local expected=$6
    
    echo -n "$num. $name... "
    OUTPUT=$($BIN $cmd $param $value --no-color 2>&1 | grep -i "raw" -A 2 | tr '\n' ' ' || true)
    
    if echo "$OUTPUT" | grep -qi "$expected"; then
        echo "✓"
        ((PASS++))
        TESTS+=("✓ $num: $name")
    else
        echo "✗"
        ((FAIL++))
        TESTS+=("✗ $num: $name")
    fi
}

echo "═══════════════════════════════════════════════════════════════"
echo "    vMSC Test Suite: Complete Generators (24/24)"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# P27: MM Layer (3/3)
run_test "1" "MM NULL" "--send-dtap-mm-null" "05 30"
run_test "2" "MM LU Request" "--send-lu-request" "05 08"
run_test "3" "MM Auth Request" "--send-dtap-mm-auth-req" "05 12"

# P20: RR Layer (3/3)
run_test "4" "RR Paging Response" "--send-paging-response" "05 27"
run_test "5" "RR Channel Request" "--send-rr-channel-request" "06 23"
run_test "6" "RR Handover Complete" "--send-dtap-rr-handover-complete" "06 2c"

# P23: CC Layer (3/3)
run_test "7" "CC Setup (MO)" "--send-dtap-cc-setup-mo" "03 05"
run_test "8" "CC Alerting" "--send-dtap-cc-alerting" "03 01"
run_test "9" "CC Disconnect" "--send-dtap-cc-disconnect" "03 25"

# P42: SMS RP (5/5 core) + MMS (3/3 variants) = 8/8
run_test "10" "SMS RP-DATA (MO)" "--send-sms-rp-data-mo" "00"
run_test "11" "SMS RP-DATA (MT)" "--send-sms-rp-data-mt" "01"
run_test "12" "SMS RP-ACK" "--send-sms-rp-ack" "02"
run_test "13" "SMS RP-ERROR" "--send-sms-rp-error" "03"
run_test "14" "SMS RP-SMMA" "--send-sms-rp-smma" "04"
run_test "15" "SMS MMS-DATA (MO)" "--send-sms-mms-data-mo" "05"
run_test "16" "SMS MMS-DATA (MT)" "--send-sms-mms-data-mt" "06"
run_test "17" "SMS MMS-ACK" "--send-sms-mms-ack" "07"

# P41: SI 16-20 (5/5)
run_test "18" "SI 16 BiCC" "--send-si-bicc" "10"
run_test "19" "SI 17 DUP" "--send-si-dup" "11"
run_test "20" "SI 18 TUP" "--send-si-tup" "12"
run_test "21" "SI 19 ISOMAP" "--send-si-isomap" "13"
run_test "22" "SI 20 ITUUP" "--send-si-ituup" "14"

# P38: BSSMAP Layer (2/2)
run_test "23" "BSSMAP RESET" "--send-bssmap-reset" "00 04 30"
run_test "24" "BSSMAP HO Complete" "--send-bssmap-ho-complete" "00 01 14"
echo ""

# ============================================================================
# Extended Parameter Validation Tests (from test_generators.sh)
# ============================================================================

echo "═══════════════════════════════════════════════════════════════"
echo "PARAMETER VALIDATION TESTS"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Test SMS parameters
test_param "25" "SMS RP-ACK with msg-ref=42" "--send-sms-rp-ack" "--sms-msg-ref" "42" "2a"
test_param "26" "SMS RP-ERROR with cause=27" "--send-sms-rp-error" "--rp-cause" "27" "1b"
test_param "27" "SI BiCC with billing_id=99" "--send-si-bicc" "--si-billing-id" "99" "63"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "MASTER TEST SUITE SUMMARY:"
echo "  Core Generator Tests: 24"
echo "  Parameter Validation Tests: 3"
echo "  Total Tests: 27"
echo "  Passed: $PASS ✓"
echo "  Failed: $FAIL ✗"
echo "═══════════════════════════════════════════════════════════════"

if [ $FAIL -eq 0 ]; then
    echo "✓✓✓ ALL 27/27 TESTS PASSED (100%)! ✓✓✓"
    exit 0
else
    echo "Failed tests:"
    for test in "${TESTS[@]}"; do
        if [[ "$test" == "✗"* ]]; then
            echo "  $test"
        fi
    done
    exit 1
fi
