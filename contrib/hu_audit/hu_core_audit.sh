#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# HU-ONLY CORE AUDIT SCRIPT
# ═══════════════════════════════════════════════════════════════════════════════
#
# Purpose: Verify that the PIVHU codebase is 100% HU-only with zero PIVX legacy
#
# Usage:
#   ./contrib/hu_audit/hu_core_audit.sh [--verbose] [--fix-suggestions]
#
# Exit codes:
#   0 = HU-ONLY CORE VERIFIED (all checks pass)
#   1 = LEGACY VIOLATION FOUND (forbidden patterns detected)
#   2 = HU COMPONENTS MISSING (required patterns not found)
#
# ═══════════════════════════════════════════════════════════════════════════════

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Script directory and repo root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Options
VERBOSE=false
FIX_SUGGESTIONS=false

for arg in "$@"; do
    case $arg in
        --verbose|-v)
            VERBOSE=true
            ;;
        --fix-suggestions|-f)
            FIX_SUGGESTIONS=true
            ;;
    esac
done

# Counters
FORBIDDEN_FOUND=0
REQUIRED_MISSING=0
TOTAL_FORBIDDEN_MATCHES=0

# ═══════════════════════════════════════════════════════════════════════════════
# FORBIDDEN PATTERNS - Legacy PIVX code that must NOT exist
# ═══════════════════════════════════════════════════════════════════════════════

declare -a FORBIDDEN_PATTERNS=(
    # ═══ LLMQ/DKG/BLS/ChainLocks (PIVHU uses HU Finality with ECDSA) ═══
    "InitLLMQSystem"
    "DestroyLLMQSystem"
    "InterruptLLMQSystem"
    "quorums_init"
    "quorums_dkgsession"
    "quorums_blockprocessor"
    "quorums_commitment"
    "quorums_debug"
    "quorums_signing"
    "quorums_chainlocks"
    "quorums_instantsend"
    "CLLMQUtils"
    "CDKGSession"
    "CDKGMember"
    "CDKGContribution"
    "CDKGComplaint"
    "CDKGJustification"
    "CDKGPrematureCommitment"
    "CFinalCommitment"
    "CRecoveredSig"
    "CChainLockSig"
    "chainLocksHandler"
    "signingManager"
    "ChainLock"
    "chainlock"
    "CHAINLOCK"
    "InstantSend"
    "instantsend"
    "INSTANTSEND"
    "llmq::InitLLMQSystem"
    "llmq::DestroyLLMQSystem"
    "BLSInit"
    "BLSCleanup"
    "BLS_WRAPPER"
    "bls_wrapper"
    "CBLSPublicKey"
    "CBLSSecretKey"
    "CBLSSignature"
    "CBLSLazyPublicKey"
    "CBLSLazySignature"
    "bls/bls_wrapper.h"
    "bls/bls_ies.h"
    "chiabls"

    # ═══ PoW/PoS consensus (PIVHU uses MN-only DMM) ═══
    "ProofOfWork"
    "ProofOfStake"
    "IsProofOfWork"
    "IsProofOfStake"

    # ═══ CoinStake (PIVHU has no PoS staking) ═══
    "CoinStake"
    "coinstake"
    "IsCoinStake"
    "fCoinStake"
    "stakeinput"
    "CStakeableOutput"
    "StakeInput"

    # ═══ Cold Staking (completely removed) ═══
    "ColdStaking"
    "coldstaking"
    "ColdStake"
    "P2CS"
    "IsPayToColdStaking"
    "CheckColdStake"
    "TX_COLDSTAKE"
    "MIN_COLDSTAKING_AMOUNT"
    "P2CSDelegation"
    "CDelegation"
    "DelegatedBalance"

    # ═══ Zerocoin (completely removed) ═══
    "zerocoin"
    "Zerocoin"
    "zPIV"
    "zpiv"
    "StakeZPIV"
    "StakeMint"
    "SPORK_16_ZEROCOIN"
    "SPORK_18_ZEROCOIN"

    # ═══ Budget/Superblock (replaced by DAO T-direct) ═══
    "Superblock"
    "superblock"
    "masternode-budget"
    "superblocks"
    "CBudget"
    "CSuperblock"

    # ═══ Cold staking spork ═══
    "SPORK_19_COLDSTAKING"

    # ═══ Legacy SPORKs (completely removed in HU) ═══
    "SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT"
    "SPORK_13_ENABLE_SUPERBLOCKS"
    "SPORK_14_NEW_PROTOCOL_ENFORCEMENT"
    "SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2"
    "SPORK_21_LEGACY_MNS_MAX_HEIGHT"

    # ═══ Legacy upgrade references ═══
    "legacy staking"
    "legacy budget"
    "legacy zerocoin"
    "V5.5.0"
    "V5.6.0"
    "v5.5"
    "v5.6"
    "toggle between"
    "transition to DMN"
)

# Patterns that are allowed in specific contexts (exceptions)
declare -a EXCEPTION_PATTERNS=(
    # These are allowed because they're part of the audit itself
    "hu_core_audit"
    "HU-CORE-AUDIT"
    # Historical references in docs/changelogs are OK
    "CHANGELOG"
    "RELEASE-NOTES"
)

# ═══════════════════════════════════════════════════════════════════════════════
# REQUIRED PATTERNS - HU components that MUST exist
# ═══════════════════════════════════════════════════════════════════════════════

declare -A REQUIRED_PATTERNS=(
    # HU State Machine
    ["HuGlobalState"]="HU State Machine structure"
    ["CheckInvariants"]="Invariant verification function"

    # HU State fields (at least some should exist)
    ["R_annual"]="R% annual rate field"
    ["R_next"]="Next R% field for DOMC"

    # HU Transitions
    ["KHU_MINT"]="MINT transaction type"
    ["KHU_REDEEM"]="REDEEM transaction type"
    ["KHU_STAKE"]="STAKE transaction type"
    ["KHU_UNSTAKE"]="UNSTAKE transaction type"

    # DOMC governance
    ["KHU_DOMC"]="DOMC transaction types"

    # RPC Commands
    ["khumint"]="MINT RPC command"
    ["khuredeem"]="REDEEM RPC command"
    ["khustake"]="STAKE RPC command"
    ["khuunstake"]="UNSTAKE RPC command"
    ["getkhustate"]="Get KHU state RPC"
    ["khubalance"]="KHU balance RPC"

    # Consensus - MN-only with HU Finality (ECDSA-based, not BLS)
    ["HuStateCommitment"]="HU Finality state commitment"
    ["hu_finality"]="HU Finality module"
    ["deterministicmns"]="Deterministic Masternodes"

    # Sapling (privacy layer)
    ["SaplingNote"]="Sapling note structure"
    ["SaplingMerkleTree"]="Sapling Merkle tree"
)

# ═══════════════════════════════════════════════════════════════════════════════
# HELPER FUNCTIONS
# ═══════════════════════════════════════════════════════════════════════════════

print_header() {
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}║${NC} ${BOLD}$1${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
}

print_section() {
    echo ""
    echo -e "${BLUE}─── $1 ───${NC}"
}

print_ok() {
    echo -e "  ${GREEN}[OK]${NC} $1"
}

print_fail() {
    echo -e "  ${RED}[FAIL]${NC} $1"
}

print_warn() {
    echo -e "  ${YELLOW}[WARN]${NC} $1"
}

print_info() {
    if [ "$VERBOSE" = true ]; then
        echo -e "  ${CYAN}[INFO]${NC} $1"
    fi
}

# Check if a file should be excluded from scanning
should_exclude() {
    local file="$1"

    # Exclude this audit script itself
    if [[ "$file" == *"hu_core_audit"* ]]; then
        return 0
    fi

    # Exclude audit documentation
    if [[ "$file" == *"HU-CORE-AUDIT"* ]]; then
        return 0
    fi

    # Exclude changelogs and release notes
    if [[ "$file" == *"CHANGELOG"* ]] || [[ "$file" == *"RELEASE"* ]]; then
        return 0
    fi

    # Exclude external dependencies
    if [[ "$file" == *"/depends/"* ]]; then
        return 0
    fi

    # Exclude chiabls (external library)
    if [[ "$file" == *"/chiabls/"* ]]; then
        return 0
    fi

    # Exclude leveldb (external library)
    if [[ "$file" == *"/leveldb/"* ]]; then
        return 0
    fi

    # Exclude secp256k1 (external library)
    if [[ "$file" == *"/secp256k1/"* ]]; then
        return 0
    fi

    # Exclude univalue (external library)
    if [[ "$file" == *"/univalue/"* ]]; then
        return 0
    fi

    # Exclude Qt locale/translation files
    if [[ "$file" == *"/qt/locale/"* ]]; then
        return 0
    fi

    return 1
}

# ═══════════════════════════════════════════════════════════════════════════════
# FORBIDDEN PATTERN SCANNER
# ═══════════════════════════════════════════════════════════════════════════════

scan_forbidden_patterns() {
    print_section "Scanning for FORBIDDEN legacy patterns"

    local found_any=false

    for pattern in "${FORBIDDEN_PATTERNS[@]}"; do
        print_info "Checking pattern: $pattern"

        # Search in src/, test/ directories
        local matches=$(grep -rn --include="*.cpp" --include="*.h" --include="*.ui" \
            "$pattern" "$REPO_ROOT/src" 2>/dev/null || true)

        if [ -n "$matches" ]; then
            # Filter out excluded files
            local filtered_matches=""
            while IFS= read -r line; do
                local file=$(echo "$line" | cut -d: -f1)
                if ! should_exclude "$file"; then
                    filtered_matches+="$line"$'\n'
                fi
            done <<< "$matches"

            # Remove trailing newline
            filtered_matches=$(echo -n "$filtered_matches" | sed '/^$/d')

            if [ -n "$filtered_matches" ]; then
                found_any=true
                FORBIDDEN_FOUND=$((FORBIDDEN_FOUND + 1))
                local count=$(echo "$filtered_matches" | wc -l)
                TOTAL_FORBIDDEN_MATCHES=$((TOTAL_FORBIDDEN_MATCHES + count))

                print_fail "Legacy pattern \"$pattern\" found ($count occurrences):"

                if [ "$VERBOSE" = true ]; then
                    echo "$filtered_matches" | while IFS= read -r line; do
                        echo -e "       ${YELLOW}$line${NC}"
                    done
                else
                    # Show only first 3 matches in non-verbose mode
                    echo "$filtered_matches" | head -3 | while IFS= read -r line; do
                        echo -e "       ${YELLOW}$line${NC}"
                    done
                    if [ "$count" -gt 3 ]; then
                        echo -e "       ${YELLOW}... and $((count - 3)) more${NC}"
                    fi
                fi
            fi
        fi
    done

    if [ "$found_any" = false ]; then
        print_ok "No legacy patterns found"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# REQUIRED PATTERN CHECKER
# ═══════════════════════════════════════════════════════════════════════════════

check_required_patterns() {
    print_section "Checking for REQUIRED HU patterns"

    for pattern in "${!REQUIRED_PATTERNS[@]}"; do
        local description="${REQUIRED_PATTERNS[$pattern]}"
        print_info "Checking: $pattern ($description)"

        # Search in src/ directory
        local matches=$(grep -rn --include="*.cpp" --include="*.h" \
            "$pattern" "$REPO_ROOT/src" 2>/dev/null | head -1 || true)

        if [ -n "$matches" ]; then
            print_ok "$description ($pattern)"
        else
            REQUIRED_MISSING=$((REQUIRED_MISSING + 1))
            print_warn "Missing: $description ($pattern)"
        fi
    done
}

# ═══════════════════════════════════════════════════════════════════════════════
# HU STATE MACHINE VERIFICATION
# ═══════════════════════════════════════════════════════════════════════════════

verify_hu_state_machine() {
    print_section "Verifying HU State Machine integrity"

    # Check for HuGlobalState structure
    local state_file=$(find "$REPO_ROOT/src" -name "*.h" -exec grep -l "struct HuGlobalState" {} \; 2>/dev/null | head -1)

    if [ -n "$state_file" ]; then
        print_ok "HuGlobalState structure found in: $(basename $state_file)"

        # Verify required fields
        local fields=("int64_t C" "int64_t U" "int64_t Z" "int64_t Cr" "int64_t Ur" "int64_t T")
        for field in "${fields[@]}"; do
            if grep -q "$field" "$state_file" 2>/dev/null; then
                print_info "  Field found: $field"
            else
                print_warn "  Field missing: $field"
            fi
        done
    else
        print_warn "HuGlobalState structure not found"
    fi

    # Check for invariant verification
    local invariant_check=$(grep -rn "C == U + Z\|C == U+Z" "$REPO_ROOT/src" --include="*.cpp" --include="*.h" 2>/dev/null | head -1)
    if [ -n "$invariant_check" ]; then
        print_ok "Invariant C == U + Z verified"
    else
        print_warn "Invariant C == U + Z not found in code"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# MN-ONLY CONSENSUS VERIFICATION (HU Finality - ECDSA-based)
# ═══════════════════════════════════════════════════════════════════════════════

verify_mn_consensus() {
    print_section "Verifying MN-only consensus components (HU Finality)"

    # Check that LLMQ directory is REMOVED (forbidden)
    if [ -d "$REPO_ROOT/src/llmq" ]; then
        print_fail "LLMQ directory still exists (should be removed for HU Finality)"
        FORBIDDEN_FOUND=$((FORBIDDEN_FOUND + 1))
    else
        print_ok "LLMQ directory removed (HU Finality uses ECDSA)"
    fi

    # Check that BLS directory is REMOVED (forbidden)
    if [ -d "$REPO_ROOT/src/bls" ]; then
        print_fail "BLS directory still exists (should be removed for HU Finality)"
        FORBIDDEN_FOUND=$((FORBIDDEN_FOUND + 1))
    else
        print_ok "BLS directory removed (HU Finality uses ECDSA)"
    fi

    # Check HU Finality module
    if [ -f "$REPO_ROOT/src/hu/hu_finality.h" ]; then
        print_ok "HU Finality header present"
    else
        print_warn "HU Finality header not found (src/hu/hu_finality.h)"
    fi

    if [ -f "$REPO_ROOT/src/hu/hu_finality.cpp" ]; then
        print_ok "HU Finality implementation present"
    else
        print_warn "HU Finality implementation not found (src/hu/hu_finality.cpp)"
    fi

    # Check Deterministic Masternodes
    if [ -f "$REPO_ROOT/src/evo/deterministicmns.cpp" ]; then
        print_ok "Deterministic Masternodes implementation present"
    else
        print_warn "Deterministic Masternodes implementation not found"
    fi

    # Check for HuStateCommitment (ECDSA-based finality)
    local hu_commit=$(grep -rn "HuStateCommitment" "$REPO_ROOT/src" --include="*.h" --include="*.cpp" 2>/dev/null | head -1)
    if [ -n "$hu_commit" ]; then
        print_ok "HuStateCommitment (ECDSA-based finality) found"
    else
        print_warn "HuStateCommitment not found"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# SAPLING / ZKHU VERIFICATION
# ═══════════════════════════════════════════════════════════════════════════════

verify_sapling() {
    print_section "Verifying Sapling/ZKHU components"

    # Check Sapling directory
    if [ -d "$REPO_ROOT/src/sapling" ]; then
        local sapling_files=$(ls "$REPO_ROOT/src/sapling"/*.cpp 2>/dev/null | wc -l)
        print_ok "Sapling directory present ($sapling_files source files)"
    else
        print_warn "Sapling directory not found"
    fi

    # Check for ZKHU-related code
    local zkhu_refs=$(grep -rn "ZKHU\|KHU_STAKE" "$REPO_ROOT/src" --include="*.cpp" --include="*.h" 2>/dev/null | wc -l)
    if [ "$zkhu_refs" -gt 0 ]; then
        print_ok "ZKHU/KHU_STAKE references found ($zkhu_refs occurrences)"
    else
        print_warn "No ZKHU/KHU_STAKE references found"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# RPC COMMANDS VERIFICATION
# ═══════════════════════════════════════════════════════════════════════════════

verify_rpc_commands() {
    print_section "Verifying HU RPC commands"

    local rpc_file="$REPO_ROOT/src/wallet/rpc_hu.cpp"

    if [ -f "$rpc_file" ]; then
        print_ok "HU RPC file present: rpc_hu.cpp"

        # Check for specific commands
        local commands=("khumint" "khuredeem" "khustake" "khuunstake" "khubalance" "getkhustate" "khuliststaked")
        for cmd in "${commands[@]}"; do
            if grep -q "\"$cmd\"" "$rpc_file" 2>/dev/null; then
                print_info "  Command found: $cmd"
            else
                print_warn "  Command missing: $cmd"
            fi
        done
    else
        print_warn "HU RPC file not found"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# SUMMARY AND RESULT
# ═══════════════════════════════════════════════════════════════════════════════

print_summary() {
    print_header "HU-ONLY CORE AUDIT SUMMARY"

    echo ""
    echo -e "  Repository: ${BOLD}$REPO_ROOT${NC}"
    echo -e "  Commit:     ${BOLD}$(cd $REPO_ROOT && git rev-parse --short HEAD 2>/dev/null || echo 'N/A')${NC}"
    echo -e "  Branch:     ${BOLD}$(cd $REPO_ROOT && git branch --show-current 2>/dev/null || echo 'N/A')${NC}"
    echo -e "  Date:       ${BOLD}$(date '+%Y-%m-%d %H:%M:%S')${NC}"
    echo ""

    if [ "$FORBIDDEN_FOUND" -eq 0 ]; then
        echo -e "  ${GREEN}[OK]${NC} No legacy patterns found (0 violations)"
    else
        echo -e "  ${RED}[FAIL]${NC} Legacy patterns found: $FORBIDDEN_FOUND types, $TOTAL_FORBIDDEN_MATCHES total matches"
    fi

    if [ "$REQUIRED_MISSING" -eq 0 ]; then
        echo -e "  ${GREEN}[OK]${NC} All required HU patterns present"
    else
        echo -e "  ${YELLOW}[WARN]${NC} Missing HU patterns: $REQUIRED_MISSING"
    fi

    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"

    if [ "$FORBIDDEN_FOUND" -gt 0 ]; then
        echo ""
        echo -e "  ${RED}${BOLD}RESULT: HU-ONLY CORE VIOLATION ❌${NC}"
        echo ""
        echo -e "  ${RED}Legacy PIVX code detected. This codebase is NOT HU-only.${NC}"
        echo -e "  ${RED}Please remove all legacy patterns before proceeding.${NC}"
        echo ""
        echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
        return 1
    elif [ "$REQUIRED_MISSING" -gt 0 ]; then
        echo ""
        echo -e "  ${YELLOW}${BOLD}RESULT: HU COMPONENTS INCOMPLETE ⚠️${NC}"
        echo ""
        echo -e "  ${YELLOW}Some HU components are missing. Core functionality may be incomplete.${NC}"
        echo ""
        echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
        return 2
    else
        echo ""
        echo -e "  ${GREEN}${BOLD}RESULT: HU-ONLY CORE VERIFIED ✅${NC}"
        echo ""
        echo -e "  ${GREEN}This codebase is 100% HU-only with all required components.${NC}"
        echo ""
        echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
        return 0
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# MAIN EXECUTION
# ═══════════════════════════════════════════════════════════════════════════════

main() {
    print_header "HU-ONLY CORE AUDIT"
    echo ""
    echo -e "  ${CYAN}Scanning PIVHU codebase for legacy PIVX patterns...${NC}"
    echo -e "  ${CYAN}Verifying HU-only architecture compliance...${NC}"

    # Change to repo root
    cd "$REPO_ROOT"

    # Run all checks
    scan_forbidden_patterns
    check_required_patterns
    verify_hu_state_machine
    verify_mn_consensus
    verify_sapling
    verify_rpc_commands

    # Print summary and exit with appropriate code
    print_summary
    exit $?
}

# Run main function
main
