# vMSC

GSM/SIGTRAN protocol message generator and simulator.

---

## Флаги командной строки

### Просмотр интерфейсов

| Флаг | Описание |
|---|---|
| `--show-interfaces` | Все 7 интерфейсов + `[network]` + `[gt-route]` |
| `--show-a-interface` | A-interface (MSC ↔ BSC) |
| `--show-c-interface` | C-interface (MSC ↔ HLR) |
| `--show-f-interface` | F-interface (MSC ↔ EIR) |
| `--show-e-interface` | E-interface (MSC ↔ MSC) |
| `--show-nc-interface` | Nc-interface (MSC-S ↔ MGW) |
| `--show-isup-interface` | ISUP-interface (MSC ↔ PSTN/GW) |
| `--show-gs-interface` | Gs-interface (MSC ↔ SGSN) |
| `--show-gt-route` | Таблица GT-маршрутизации SCCP (`[gt-route]`) |
| `--show-network` | Сетевые параметры: маски, GW, NTP |
| `--show-subscriber` | Абонентские параметры: IMSI, MSISDN, msc_gt |
| `--show-identity` | Идентификация узла: MCC, MNC, LAC, Cell ID |
| `--show-m3ua` | Параметры M3UA: OPC/DPC/NI/SI/SLS/MP |
| `--show-bssmap` | Параметры BSSMAP |
| `--show-transport` | Транспортные параметры: IP/порты/маски/GW |
| `--show-encapsulation` | Цепочка инкапсуляции сообщения |

### Отправка сообщений

| Флаг | Описание |
|---|---|
| `--send-udp` | Отправить сообщение по UDP |
| `--send-clear` | Отправить BSSMAP Clear Command |
| `--send-map-sai` | MAP SendAuthenticationInfo (opCode=56, C-interface, MSC→HLR) |
| `--send-map-ul` | MAP UpdateLocation (opCode=2, C-interface, MSC→HLR) |
| `--send-map-sri` | MAP SendRoutingInfo (opCode=22, C-interface, GMSC→HLR, MO-call) |
| `--send-map-prn` | MAP ProvideRoamingNumber (opCode=4, C-interface, GMSC→HLR, MT-call) |
| `--send-map-cl` | MAP CancelLocation (opCode=3, C-interface, HLR→VLR) |
| `--send-map-isd` | MAP InsertSubscriberData (opCode=7, C-interface, HLR→VLR) |
| `--send-map-purge-ms` | MAP PurgeMS (opCode=67, C-interface, VLR→HLR) |
| `--send-map-auth-failure-report` | MAP AuthenticationFailureReport (opCode=15, C-interface, VLR→HLR) |
| `--failure-cause <N>` | FailureCause для AuthFailureReport: `0`=wrongUserResponse (умолч.), `1`=wrongNetworkSignature |
| `--send-map-check-imei` | MAP CheckIMEI (opCode=43, F-interface, MSC→EIR) |
| `--send-map-prepare-ho` | MAP PrepareHandover (opCode=68, E-interface, Anchor→Relay MSC) |
| `--send-map-end-signal` | MAP SendEndSignal (opCode=29, E-interface, Relay→Anchor MSC) |
| `--send-gs-lu` | BSSAP+ LocationUpdate (0x01, Gs-interface, MSC→SGSN) || `--send-gs-paging` | BSSAP+ MS Paging Request (0x09, Gs-interface, MSC → SGSN) |
| `--send-gs-imsi-detach` | BSSAP+ IMSI Detach Indication (0x05, Gs-interface, MSC → SGSN) |
| `--send-gs-reset` | BSSAP+ Reset (0x0B, Gs-interface, MSC → SGSN) |
| `--send-gs-reset-ack` | BSSAP+ Reset Acknowledge (0x0C, Gs-interface) |
| `--gs-reset-cause <N>` | Reset Cause: `0`=power-on (умолч.), `1`=om-intervention, `2`=load-control |
| `--gs-detach-type <N>` | Detach Type: `0`=power-off (умолч.), `1`=reattach-required, `2`=GPRS-detach |
| `--gs-peer-number <E.164>` | VLR/SGSN E.164 номер для Paging/Detach (умолч: msc_gt) || `--send-map-sai-end` | MAP SAI ReturnResultLast — TCAP End с фиктивным триплетом (HLR→MSC симуляция) |
| `--send-map-ul-end` | MAP UL ReturnResultLast — TCAP End с hlr-Number (HLR→MSC симуляция) |
| `--send-tcap-end` | Голый TCAP End (AARE accepted, без компонентов) |
| `--send-tcap-continue` | TCAP Continue (промежуточный шаг диалога) |
| `--send-tcap-abort` | TCAP Abort (P-Abort) — принудительное прерывание транзакции |
| `--send-map-return-error` | MAP ReturnError в TCAP End — симуляция MAP-ошибки от партнёра |
| `--send-isup-iam` | ISUP IAM Initial Address Message (ISUP-interface, MSC → PSTN/GW, без SCCP) |
| `--send-isup-acm` | ISUP ACM Address Complete Message (ISUP-interface, PSTN/GW → MSC, абонент вызывается) |
| `--send-isup-anm` | ISUP ANM Answer Message (ISUP-interface, PSTN/GW → MSC, абонент ответил) |
| `--send-isup-rel` | ISUP REL Release Message (ISUP-interface, MSC ↔ PSTN/GW, без SCCP) |
| `--send-isup-rlc` | ISUP RLC Release Complete (ISUP-interface, PSTN/GW → MSC, подтверждение REL) |
| `--send-isup-con` | ISUP CON Connect (MT=0x07, ISUP-interface, BCI: subscriber free) |
| `--send-isup-cpg` | ISUP CPG Call Progress (MT=0x2C, ISUP-interface, EventInfo: alerting) |
| `--send-isup-sus` | ISUP SUS Suspend (MT=0x0D, ISUP-interface, приостановка B-канала) |
| `--send-isup-res` | ISUP RES Resume (MT=0x0E, ISUP-interface, возобновление B-канала) |
| `--sus-cause <N>` | SUS/RES Initiating Side: `0`=network initiated (умолч.), `1`=ISDN subscriber |
| `--send-map-prep-subseq-ho` | MAP PrepareSubsequentHandover (opCode=69, E-interface, Anchor→Target MSC) |
| `--send-map-process-access-sig` | MAP ProcessAccessSignalling (opCode=33, E-interface, Target→Anchor MSC) |
| `--send-map-mo-fsm` | MAP MO-ForwardSM (opCode=46, C-interface, MSC → SMSC, TP-Submit TPDU) |
| `--send-map-mt-fsm` | MAP MT-ForwardSM (opCode=44, C-interface, SMSC → MSC, TP-Deliver TPDU) |
| `--send-map-ussd` | MAP processUnstructuredSS-Request (opCode=59, C-interface, MSC → HLR) |
| `--send-map-sri-sm` | MAP SendRoutingInfoForSM (opCode=45, C-interface, GMSC → HLR) |
| `--send-map-report-smds` | MAP ReportSMDeliveryStatus (opCode=47, C-interface, SMSC → HLR) |
| `--smsc <E.164>` | Адрес SMSC для SRI-SM / ReportSMDeliveryStatus (умолч. `79161000099`) |
| `--smds-outcome <N>` | SM-DeliveryOutcome: `0`=memCapacityExceeded `1`=absentSubscriber `2`=successfulTransfer (умолч.) |
| `--send-dtap-auth-req` | DTAP Authentication Request (MM 0x12, A-interface, MSC → MS) |
| `--send-dtap-auth-resp` | DTAP Authentication Response (MM 0x14, A-interface, MS → MSC) |
| `--send-dtap-id-req` | DTAP Identity Request (MM 0x18, A-interface, MSC → MS) |
| `--send-dtap-id-resp` | DTAP Identity Response (MM 0x19, A-interface, MS → MSC) |
| `--send-bssmap-cipher` | BSSMAP Ciphering Mode Command (0x35, A-interface, MSC → BSC) |
| `--send-dtap-lu-accept` | DTAP Location Updating Accept (MM 0x02, A-interface, MSC → MS) |
| `--send-dtap-lu-reject` | DTAP Location Updating Reject (MM 0x04, A-interface, MSC → MS) |
| `--send-bssmap-reset` | BSSMAP Reset (0x30, A-interface, MSC → BSC) |
| `--send-bssmap-assign-req` | BSSMAP Assignment Request (0x01, A-interface, MSC → BSC) |
| `--send-bssmap-assign-compl` | BSSMAP Assignment Complete (0x02, A-interface, BSC → MSC) |
| `--send-bssmap-clear-req` | BSSMAP Clear Request (0x22, A-interface, BSC → MSC) |
| `--send-bssmap-clear-compl` | BSSMAP Clear Complete (0x21, A-interface, BSC → MSC) |
| `--send-bssmap-paging` | BSSMAP Paging (0x52, A-interface, MSC → BSC) |
| `--speech-ver <hex>` | Речевой кодек для Assignment Request: `0x01`=FR v1 (умолч.), `0x21`=EFR, `0x41`=AMR-FR |
| `--send-dtap-cm-srv-req` | MM CM Service Request (0x24, A-interface, MS → MSC) |
| `--send-dtap-cm-srv-acc` | MM CM Service Accept (0x21, A-interface, MSC → MS) |
| `--send-dtap-cc-setup-mo` | DTAP CC Setup MO (0x05, A-interface, MS → MSC) |
| `--send-dtap-cc-setup-mt` | DTAP CC Setup MT (0x05, A-interface, MSC → MS) |
| `--send-dtap-cc-call-proc` | DTAP CC Call Proceeding (0x02, A-interface, MSC → MS) |
| `--send-dtap-cc-alerting` | DTAP CC Alerting (0x01, A-interface, MSC → MS) |
| `--send-dtap-cc-connect` | DTAP CC Connect (0x07, A-interface) |
| `--send-dtap-cc-connect-ack` | DTAP CC Connect Acknowledge (0x0F, A-interface, MSC → MS) |
| `--send-dtap-cc-disconnect` | DTAP CC Disconnect (0x25, A-interface) |
| `--send-dtap-cc-release` | DTAP CC Release (0x2D, A-interface) |
| `--send-dtap-cc-rel-compl` | DTAP CC Release Complete (0x2A, A-interface) |
| `--ti <N>` | Transaction Identifier 0–7 для CC-сообщений (умолч. 0) |
| `--cc-cause <N>` | Q.850 CC cause: `16`=normalClearing (умолч.), `17`=userBusy, `18`=noAnswer, `21`=callRejected |
| `--ms-to-net` | Направление MS→MSC для Connect/Disconnect/Release/ReleaseComplete (умолч. MSC→MS) |
| `--dtid <N>` | Destination Transaction ID для End/Continue/Abort (dec или 0x...) |
| `--otid <N>` | Originating Transaction ID для Continue |
| `--abort-cause <N>` | P-AbortCause: 0=unrecognisedMessageType 1=unrecognisedTransactionID 2=badlyFormattedTxPortion 3=incorrectTxPortion 4=resourceLimitation |
| `--invoke-id <N>` | Invoke ID для ReturnError (соответствует Invoke из Begin партнёра) |
| `--error-code <N>` | MAP error code для ReturnError: 1=systemFailure 3=dataMissing 5=unexpectedDataValue 6=unknownSubscriber 8=numberChanged 10=callBarred 13=roamingNotAllowed 21=facilityNotSupported |
| `--cic <N>` | Circuit Identification Code для ISUP IAM/REL (по умолчанию 1) |
| `--cause <N>` | Q.850 cause для ISUP REL: 16=normalClearing 17=userBusy 18=noAnswer 20=subscriberAbsent 21=callRejected 34=noCircuit 41=temporaryFailure |
| `--sm-text "<текст>"` | Текст SMS для MO/MT-ForwardSM (GSM-7, до 160 символов, умолч. `Hello from vMSC`) |
| `--ussd-str "<текст>"` | USSD-строка для processUnstructuredSS-Request (GSM-7, умолч. `*100#`) |
| `--rand <32hex>` | RAND — 16 байт случайного вызова для Authentication Request (32 hex-символа, умолч. `A0A1...AEAF`) |
| `--sres <8hex>` | SRES — 4 байта подписи MS для Authentication Response (8 hex-символов, умолч. `01020304`) |
| `--cksn <N>` | Ciphering Key Sequence Number для Authentication Request (0–6, умолч. 0) |
| `--tmsi <hex>` | TMSI для Location Updating Accept (hex/dec, умолч. `0x01020304`) |
| `--id-type <N>` | Тип идентификатора для Identity Request: 1=IMSI (умолч.) 2=IMEI 3=IMEISV 4=TMSI |
| `--lu-cause <hex>` | Причина отказа для Location Updating Reject: 0x02=IMSIunknownHLR 0x03=illegalMS (умолч.) 0x06=illegalME 0x0B=PLMNnotAllowed 0x0C=LAnotAllowed 0x0D=roamingNotAllowed |
| `--cipher-alg <hex>` | Разрешённые алгоритмы шифрования для Ciphering Mode Command: 0x01=noEncrypt 0x02=A5/1 (умолч.) 0x04=A5/2 0x08=A5/3 |

Для реальной отправки всегда требуются: `--send-udp --use-m3ua`

### TCAP Transaction ID (TID)

`--dtid` — это **OTID из входящего пакета партнёра**. В реальном сценарии он
приходит в ответе по UDP и снимается Wireshark'ом. При loopback-тестировании
подставляется вручную.

```
MSC (мы)                          HLR (партнёр)
   |                                    |
   |── TCAP Begin ──────────────────>   |   --send-map-sai
   |   OTID = 0x0001  (наш)            |
   |                                    |
   |   <─────────────── TCAP Continue ─|   ответ партнёра
   |                    OTID = 0xABCD  |   ← TID партнёра (TID_от_партнёра)
   |                    DTID = 0x0001  |   ← наш OTID (подтверждение)
   |                                    |
   |── TCAP End ────────────────────>   |   --send-map-sai-end --dtid 0xABCD
   |   DTID = 0xABCD  (его OTID!)      |   (ReturnResultLast — нормальное завершение)
   |                                    |
   или (аварийное завершение):
   |                                    |
   |── TCAP Abort ──────────────────>   |   --send-tcap-abort --dtid 0xABCD --abort-cause 4
   |   DTID = 0xABCD                   |   (P-Abort: resourceLimitation)
   |                                    |
   или (партнёр вернул ошибку MAP):
   |                                    |
   |── TCAP End (ReturnError) ───────>   |   --send-map-return-error --dtid 0xABCD --error-code 6
   |   DTID = 0xABCD                   |   (unknownSubscriber)
   |                                    |
```

```bash
# C-interface: MSC → HLR (TCAP Begin)
./vmsc --send-map-sai        --send-udp --use-m3ua
./vmsc --send-map-ul         --send-udp --use-m3ua
./vmsc --send-map-sri        --send-udp --use-m3ua   # SendRoutingInfo (MO-call)
./vmsc --send-map-prn        --send-udp --use-m3ua   # ProvideRoamingNumber (MT-call)
./vmsc --send-map-cl         --send-udp --use-m3ua   # CancelLocation (HLR→VLR)
./vmsc --send-map-isd        --send-udp --use-m3ua   # InsertSubscriberData (HLR→VLR)
./vmsc --send-map-purge-ms   --send-udp --use-m3ua   # PurgeMS (VLR→HLR)
./vmsc --send-map-auth-failure-report --send-udp --use-m3ua          # AuthenticationFailureReport (VLR→HLR)
./vmsc --send-map-auth-failure-report --failure-cause 1 --send-udp --use-m3ua  # wrongNetworkSignature

# C-interface: HLR → MSC (TCAP End, симуляция ответа)
./vmsc --send-map-sai-end   --dtid 0xABCD --send-udp --use-m3ua
./vmsc --send-map-ul-end    --dtid 0xABCD --send-udp --use-m3ua

# C-interface: управление диалогом
./vmsc --send-tcap-continue --otid 0x1 --dtid 0xABCD --send-udp --use-m3ua
./vmsc --send-tcap-end      --dtid 0xABCD             --send-udp --use-m3ua

# C-interface: ошибки MAP
./vmsc --send-tcap-abort        --dtid 0xABCD --abort-cause 4  --send-udp --use-m3ua
./vmsc --send-map-return-error  --dtid 0xABCD --invoke-id 1 --error-code 6  --send-udp --use-m3ua

# F-interface: MSC → EIR
./vmsc --send-map-check-imei --send-udp --use-m3ua

# E-interface: MSC ↔ MSC
./vmsc --send-map-prepare-ho --send-udp --use-m3ua
./vmsc --send-map-end-signal --send-udp --use-m3ua
# P12: E-interface MAP handover supplementary
./vmsc --send-map-prep-subseq-ho      --send-udp --use-m3ua  # PrepareSubsequentHandover (opCode=69)
./vmsc --send-map-process-access-sig  --send-udp --use-m3ua  # ProcessAccessSignalling   (opCode=33)

# Gs-interface: MSC → SGSN
./vmsc --send-gs-lu          --send-udp --use-m3ua
./vmsc --send-gs-paging      --send-udp --use-m3ua   # BSSAP+ Paging (VLR-Number = msc_gt)
./vmsc --send-gs-imsi-detach --gs-detach-type 0 --gs-peer-number 79161000099 --send-udp --use-m3ua  # IMSI Detach (power-off)
./vmsc --send-gs-reset       --gs-reset-cause 1 --send-udp --use-m3ua  # Reset (om-intervention)
./vmsc --send-gs-reset-ack   --send-udp --use-m3ua   # Reset Acknowledge

# P13: A-interface BSSMAP Handover (3GPP TS 48.008 §3.2.1.7–3.2.1.12)
# Сценарий inter-BSC HO: HO-Required → HO-Command → HO-Succeeded → HO-Performed
./vmsc --send-bssmap-ho-required   --send-udp --use-m3ua           # HO Required        (0x11, BSC→MSC)
./vmsc --send-bssmap-ho-required   --ho-cause 0x58 --ho-lac 100 --ho-cell 200 --send-udp --use-m3ua
./vmsc --send-bssmap-ho-command    --ho-lac 100 --ho-cell 200 --send-udp --use-m3ua  # HO Command (0x12, MSC→BSC)
./vmsc --send-bssmap-ho-succeeded  --send-udp --use-m3ua           # HO Succeeded       (0x14, MSC→BSC)
./vmsc --send-bssmap-ho-performed  --send-udp --use-m3ua           # HO Performed       (0x17, BSC→MSC, intra-BSC)
./vmsc --send-bssmap-ho-performed  --ho-cause 0x58 --send-udp --use-m3ua
./vmsc --send-bssmap-ho-candidate  --ho-lac 100 --send-udp --use-m3ua  # HO Candidate Enquiry (0x4F, MSC→BSC)

# P14: A-interface DTAP MM Supplementary (3GPP TS 24.008 §9.2.18/26/27, §9.1.9)
# Флоу TMSI-перераспределения: LU-Accept → TMSI-Realloc-Cmd → TMSI-Realloc-Compl
./vmsc --send-dtap-tmsi-realloc-cmd    --send-udp --use-m3ua           # TMSI Realloc Command  (MM 0x1A, MSC→MS)
./vmsc --send-dtap-tmsi-realloc-cmd    --tmsi 0xDEADBEEF --send-udp --use-m3ua
./vmsc --send-dtap-tmsi-realloc-compl  --send-udp --use-m3ua           # TMSI Realloc Complete (MM 0x1B, MS→MSC)
./vmsc --send-dtap-mm-info             --send-udp --use-m3ua           # MM Information        (MM 0x32, MSC→MS)
./vmsc --send-dtap-mm-info             --mm-tz 0x21 --send-udp --use-m3ua  # UTC+3 (Moscow)
./vmsc --send-dtap-mm-info             --mm-tz 0x00 --send-udp --use-m3ua  # UTC+0
./vmsc --send-dtap-cipher-compl        --send-udp --use-m3ua           # Ciphering Mode Complete (RR 0x32, MS→MSC)

# ISUP-interface: MSC ↔ PSTN/GW  (SI=5, без SCCP — прямо в M3UA)
# Полный call flow: IAM → ACM → ANM → REL → RLC
./vmsc --send-isup-iam                            --send-udp --use-m3ua   # IAM CIC=1 (по умолчанию)
./vmsc --send-isup-iam  --cic 42                  --send-udp --use-m3ua   # IAM CIC=42
./vmsc --send-isup-acm                            --send-udp --use-m3ua   # ACM (абонент вызывается)
./vmsc --send-isup-acm  --cic 42                  --send-udp --use-m3ua   # ACM CIC=42
./vmsc --send-isup-anm                            --send-udp --use-m3ua   # ANM (ответ)
./vmsc --send-isup-anm  --cic 42                  --send-udp --use-m3ua   # ANM CIC=42
./vmsc --send-isup-rel                            --send-udp --use-m3ua   # REL CIC=1, cause=16 (normalClearing)
./vmsc --send-isup-rel  --cic 42 --cause 17       --send-udp --use-m3ua   # REL CIC=42, cause=17 (userBusy)
./vmsc --send-isup-rlc                            --send-udp --use-m3ua   # RLC CIC=1 (подтверждение REL)
./vmsc --send-isup-rlc  --cic 42                  --send-udp --use-m3ua   # RLC CIC=42
# P11 ISUP supplementary
./vmsc --send-isup-con                            --send-udp --use-m3ua   # CON CIC=1 (Connect)
./vmsc --send-isup-cpg                            --send-udp --use-m3ua   # CPG CIC=1 (Call Progress: alerting)
./vmsc --send-isup-sus                            --send-udp --use-m3ua   # SUS CIC=1 (Suspend, network)
./vmsc --send-isup-sus  --sus-cause 1             --send-udp --use-m3ua   # SUS CIC=1 (Suspend, subscriber)
./vmsc --send-isup-res                            --send-udp --use-m3ua   # RES CIC=1 (Resume, network)

# C-interface: SMS (MAP over SCCP/M3UA)
./vmsc --send-map-mo-fsm                           --send-udp --use-m3ua  # MO-ForwardSM (текст по умолчанию)
./vmsc --send-map-mo-fsm --sm-text "Test SMS"      --send-udp --use-m3ua  # MO-ForwardSM с текстом
./vmsc --send-map-mt-fsm                           --send-udp --use-m3ua  # MT-ForwardSM
./vmsc --send-map-mt-fsm --sm-text "Welcome back" --send-udp --use-m3ua  # MT-ForwardSM с текстом

# C-interface: USSD (MAP over SCCP/M3UA)
./vmsc --send-map-ussd                             --send-udp --use-m3ua  # USSD *100# (умолч.)
./vmsc --send-map-ussd  --ussd-str "*105*1#"       --send-udp --use-m3ua  # USSD произвольная строка

# P15: C-interface MAP SMS Gateway (3GPP TS 29.002 §10.5.6)
# SMS delivery flow: GMSC → HLR (SRI-SM) → MSC (MT-FSM) → HLR (ReportSMDeliveryStatus)
./vmsc --send-map-sri-sm                                             --send-udp --use-m3ua  # SRI-SM        (opCode=45, GMSC→HLR)
./vmsc --send-map-sri-sm   --msisdn 79990000099                      --send-udp --use-m3ua  # SRI-SM с MSISDN
./vmsc --send-map-sri-sm   --smsc 79161000001                        --send-udp --use-m3ua  # SRI-SM с SMSC
./vmsc --send-map-report-smds                                        --send-udp --use-m3ua  # ReportSMDS    (opCode=47, successfulTransfer)
./vmsc --send-map-report-smds --smds-outcome 2                       --send-udp --use-m3ua  # outcome=2: successfulTransfer (умолч.)
./vmsc --send-map-report-smds --smds-outcome 1 --smsc 79161000001    --send-udp --use-m3ua  # outcome=1: absentSubscriber
./vmsc --send-map-report-smds --smds-outcome 0                       --send-udp --use-m3ua  # outcome=0: memCapacityExceeded

# A-interface: DTAP/BSSMAP — полный LU flow (GSM 04.08 / 3GPP TS 24.008, 48.008)
# Сброс интерфейса
./vmsc --send-bssmap-reset                        --send-udp --use-m3ua  # BSSMAP Reset (cause=0x00)
# Запрос идентификатора
./vmsc --send-dtap-id-req                         --send-udp --use-m3ua  # Identity Request (IMSI)
./vmsc --send-dtap-id-req  --id-type 2            --send-udp --use-m3ua  # Identity Request (IMEI)
./vmsc --send-dtap-id-resp                        --send-udp --use-m3ua  # Identity Response (IMSI из конфига)
# Аутентификация
./vmsc --send-dtap-auth-req                       --send-udp --use-m3ua  # Auth Request (RAND дефолт, CKSN=0)
./vmsc --send-dtap-auth-req  --rand DEADBEEFCAFEBABE0123456789ABCDEF --cksn 1 --send-udp --use-m3ua
./vmsc --send-dtap-auth-resp                      --send-udp --use-m3ua  # Auth Response (SRES дефолт)
./vmsc --send-dtap-auth-resp --sres AABBCCDD      --send-udp --use-m3ua  # Auth Response с явным SRES
# Шифрование
./vmsc --send-bssmap-cipher                       --send-udp --use-m3ua  # Cipher Mode Cmd (A5/1)
./vmsc --send-bssmap-cipher  --cipher-alg 0x08   --send-udp --use-m3ua  # Cipher Mode Cmd (A5/3)
# Результат регистрации
./vmsc --send-dtap-lu-accept                      --send-udp --use-m3ua  # LU Accept (TMSI=0x01020304)
./vmsc --send-dtap-lu-accept --tmsi 0xDEADBEEF   --send-udp --use-m3ua  # LU Accept с TMSI
./vmsc --send-dtap-lu-reject                      --send-udp --use-m3ua  # LU Reject (cause=illegalMS)
./vmsc --send-dtap-lu-reject --lu-cause 0x0B     --send-udp --use-m3ua  # LU Reject (PLMNnotAllowed)
# P8: Assignment / Clear / Paging
./vmsc --send-bssmap-assign-req                    --send-udp --use-m3ua  # Assignment Request (FR v1, CIC=1)
./vmsc --send-bssmap-assign-req  --speech-ver 0x21 --cic 5 --send-udp --use-m3ua  # Assignment Request (EFR, CIC=5)
./vmsc --send-bssmap-assign-compl                  --send-udp --use-m3ua  # Assignment Complete
./vmsc --send-bssmap-clear-req                     --send-udp --use-m3ua  # Clear Request (BSC→MSC)
./vmsc --send-bssmap-clear-compl                   --send-udp --use-m3ua  # Clear Complete
./vmsc --send-bssmap-paging                        --send-udp --use-m3ua  # Paging (IMSI+LAC из конфига)
# P9: CC / MM (MO-call flow)
./vmsc --send-dtap-cm-srv-req                      --send-udp --use-m3ua  # CM Service Request
./vmsc --send-dtap-cm-srv-acc                      --send-udp --use-m3ua  # CM Service Accept
./vmsc --send-dtap-cc-setup-mo                     --send-udp --use-m3ua  # CC Setup MO (msisdn из конфига)
./vmsc --send-dtap-cc-setup-mt --ti 1              --send-udp --use-m3ua  # CC Setup MT (TI=1)
./vmsc --send-dtap-cc-call-proc                    --send-udp --use-m3ua  # CC Call Proceeding
./vmsc --send-dtap-cc-alerting                     --send-udp --use-m3ua  # CC Alerting
./vmsc --send-dtap-cc-connect                      --send-udp --use-m3ua  # CC Connect (MSC→MS)
./vmsc --send-dtap-cc-connect  --ms-to-net         --send-udp --use-m3ua  # CC Connect (MS→MSC)
./vmsc --send-dtap-cc-connect-ack                  --send-udp --use-m3ua  # CC Connect Ack
./vmsc --send-dtap-cc-disconnect                   --send-udp --use-m3ua  # CC Disconnect (MSC→MS, cause=16)
./vmsc --send-dtap-cc-disconnect --ms-to-net --cc-cause 17 --send-udp --use-m3ua  # Disconnect (MS→MSC, userBusy)
./vmsc --send-dtap-cc-release                      --send-udp --use-m3ua  # CC Release
./vmsc --send-dtap-cc-rel-compl                    --send-udp --use-m3ua  # CC Release Complete
```

### Стек протоколов

| Флаг | Описание |
|---|---|
| `--use-m3ua` | Включить M3UA (автоматически включает `--use-sccp`) |
| `--use-sccp` | Включить SCCP CR (connection-oriented) |
| `--use-sccp-dt1` | Использовать SCCP DT1 вместо CR |
| `--use-bssmap-l3` | Использовать BSSMAP Complete Layer 3 (вместо DTAP) |
| `--no-bssap` | Отключить BSSAP-обёртку |
| `--use-sctp` | Использовать SCTP вместо UDP (экспериментально) |

### Параметры M3UA / GSM

| Флаг | Значение | Описание |
|---|---|---|
| `--opc <N>` | uint32 | Originating Point Code (A-interface) |
| `--dpc <N>` | uint32 | Destination Point Code (A-interface) |
| `--ni <N>` | 0/2/3 | Network Indicator: 0=International, 2=National, 3=Reserved |
| `--imsi <S>` | строка | IMSI абонента |
| `--mcc <N>` | uint16 | Mobile Country Code |
| `--mnc <N>` | uint16 | Mobile Network Code |
| `--lac <N>` | uint16 | Location Area Code |
| `--cell-id <N>` | uint16 | Cell Identity (BSSMAP) |

### Конфигурация

| Флаг | Описание |
|---|---|
| `--config <файл>` | Загрузить конфигурацию из файла (можно указывать несколько раз) |
| `--save-config` | Сохранить текущие параметры в `vmsc.conf` |

```bash
# Загрузить оба файла конфигурации
./vmsc --config vmsc.conf --config vmsc_interfaces.conf

# Переопределить параметр и сохранить
./vmsc --opc 999 --dpc 888 --ni 2 --save-config
```

### Прочее

| Флаг | Описание |
|---|---|
| `--no-lu` | Не генерировать Location Update Request |
| `--no-paging` | Не генерировать Paging Response |
| `--no-color` | Отключить цветной вывод |

---

# Конфигурационные файлы vMSC

## Обзор

vMSC поддерживает сохранение параметров в конфигурационном файле для удобного повторного использования.

## Расположение файла конфигурации

По умолчанию vMSC ищет конфигурацию в следующих местах (в порядке приоритета):
1. `~/.vmsc.conf` (домашняя директория пользователя)
2. `./vmsc.conf` (текущая директория)

Вы также можете указать собственный путь с помощью опции `--config <путь>`.

## Формат конфигурационного файла

Конфигурация использует простой INI-подобный формат:

**vmsc.conf** — абонентские параметры:
```ini
[subscriber]
imsi=250990000000001
msisdn=79990000001
```

**vmsc_interfaces.conf** — интерфейсы и сетевые параметры (включая NTP):
```ini
[network]
local_netmask=255.255.255.248
remote_netmask=255.255.255.0
gateway=100.100.100.1
ntp_primary=10.86.1.100       # основной NTP-сервер
ntp_secondary=192.168.33.3    # резервный NTP-сервер (опционально)

[A-interface]
opc=14001
dpc=14011
ni=3
...
```

## Использование

### Автоматическая загрузка

При запуске vMSC автоматически загружает конфигурацию, если файл существует:

```bash
./vmsc --send-udp --use-m3ua
```

### Сохранение текущих параметров

Сохранить текущие параметры в конфиг можно с помощью `--save-config`:

```bash
# Сохранить в ~/.vmsc.conf
./vmsc --opc 100 --dpc 200 --lac 54321 --save-config

# Сохранить в конкретный файл
./vmsc --opc 100 --dpc 200 --save-config /path/to/my-config.conf
```

### Загрузка из конкретного файла

```bash
./vmsc --config /path/to/my-config.conf --send-udp --use-m3ua
```

### Переопределение параметров из конфига

Параметры командной строки переопределяют значения из конфига:

```bash
# Использовать конфиг, но изменить OPC
./vmsc --opc 999 --send-udp --use-m3ua
```

## Приоритет параметров

1. **Аргументы командной строки** (наивысший приоритет)
2. **Конфигурационный файл** (указанный через `--config`)
3. **~/.vmsc.conf** (домашняя директория)
4. **./vmsc.conf** (текущая директория)
5. **Значения по умолчанию** (встроенные в программу)

## Примеры использования

### Пример 1: Создание стандартной конфигурации для тестов

```bash
# Настроить параметры и сохранить
./vmsc --opc 100 --dpc 200 --mcc 250 --mnc 99 --lac 12345 \
       --imsi 250991234567890 --save-config

# Теперь можно запускать без параметров
./vmsc --send-udp --use-m3ua
```

### Пример 2: Разные конфиги для разных сетей

```bash
# Сохранить конфиг для сети A
./vmsc --opc 100 --dpc 200 --mcc 250 --save-config ~/vmsc-network-a.conf

# Сохранить конфиг для сети B
./vmsc --opc 300 --dpc 400 --mcc 251 --save-config ~/vmsc-network-b.conf

# Использовать конкретную сеть
./vmsc --config ~/vmsc-network-a.conf --send-udp --use-m3ua
./vmsc --config ~/vmsc-network-b.conf --send-udp --use-m3ua
```

### Пример 3: Разные OPC/DPC для разных Network Indicators

```bash
# В конфиге прописаны Point Codes для каждого NI
# Сохранить конфигурацию
./vmsc --opc-ni0 10 --dpc-ni0 20 \
       --opc-ni2 100 --dpc-ni2 200 \
       --opc-ni3 300 --dpc-ni3 400 \
       --save-config

# Использовать International (NI=0)
./vmsc --send-udp --use-m3ua --ni 0
# Сообщения идут с OPC=10, DPC=20

# Использовать National (NI=2)
./vmsc --send-udp --use-m3ua --ni 2
# Сообщения идут с OPC=100, DPC=200
```

## Секции конфигурационного файла

### [network]
Параметры сети GSM:
- `mcc` — Mobile Country Code (например, 250 для России)
- `mnc` — Mobile Network Code (например, 99)
- `lac` — Location Area Code

### [m3ua]
Параметры M3UA/SIGTRAN с привязкой Point Codes к Network Indicator:
- `opc_ni0` — Originating Point Code для NI=0 (International)
- `dpc_ni0` — Destination Point Code для NI=0 (International)
- `opc_ni2` — Originating Point Code для NI=2 (National)
- `dpc_ni2` — Destination Point Code для NI=2 (National)
- `opc_ni3` — Originating Point Code для NI=3 (Reserved)
- `dpc_ni3` — Destination Point Code для NI=3 (Reserved)
- `ni` — Network Indicator (0=International, 2=National, 3=Reserved)
- `si` — Service Indicator: тип вложенного протокола (3=SCCP, 5=ISUP)
- `sls` — Signalling Link Selection: выбор линка для балансировки нагрузки (0–15)
- `mp` — Message Priority: приоритет при перегрузке (0=normal, 3=NM)

**Логика работы:** При генерации сообщения программа использует OPC/DPC для текущего значения NI. Это позволяет иметь разные Point Codes для разных типов сетей.

---

## Параметры MTP3 — SI, SLS, MP

Это три поля из заголовка **MTP3** (уровень транспорта ОКС-7), которые M3UA передаёт в теге `Protocol Data` (0x0210 по RFC 4666).

### SI — Service Indicator

Указывает **какой протокол** вложен в пакет. Маршрутизатор по нему понимает, кому передать данные:

| Значение | Протокол | Интерфейс |
|---|---|---|
| `0` | SNM (Signalling Network Management) | — |
| `3` | **SCCP** | A, C, F, E, Gs |
| `4` | TUP (устаревший голосовой) | — |
| `5` | **ISUP** | ISUP-interface |

В конфиге задаётся как `si=3` или `si=5`. Это единственное из трёх полей, которое **критично** для корректной работы: без `si=5` в `[isup-interface]` ISUP-пакеты были бы помечены как SCCP и отброшены на стороне получателя.

### SLS — Signalling Link Selection

Используется для **балансировки нагрузки** по SS7-линкам между двумя точками. Все сообщения с одинаковым SLS идут по одному и тому же физическому линку — это гарантирует порядок доставки для одной транзакции/вызова.

На практике MSC берёт несколько последних бит из номера транзакции (TCAP TID) или номера вызова и подставляет их в SLS — чтобы один и тот же вызов всегда шёл по одному линку, но разные вызовы распределялись по разным.

Диапазон: `0–15` (4 бита). Значение `sls=0` — стандартное для тестового трафика.

### MP — Message Priority

Приоритет обработки сообщения при перегрузке сети:

| Значение | Смысл |
|---|---|
| `0` | Normal — обычный трафик (MAP, ISUP) |
| `1` | Important — важные сообщения |
| `2` | Overload — сообщения управления перегрузкой |
| `3` | Network Management — служебные MTP3 сообщения (никогда не отбрасываются) |

При перегрузке узел отбрасывает сначала `MP=0`, затем `MP=1` и т.д. Значение `mp=0` — стандартное для пользовательского трафика.

---

## Сетевые параметры узла — маска подсети и шлюз

Задаются в секции `[network]` файла `vmsc_interfaces.conf` (вверху файла):

```ini
[network]
local_netmask=255.255.255.248   # маска локального сигнального интерфейса MSC
remote_netmask=255.255.255.0    # маска подсети на стороне оператора/партнёра
gateway=100.100.100.1           # шлюз по умолчанию
ntp_primary=10.86.1.100         # основной NTP-сервер
ntp_secondary=192.168.33.3      # резервный NTP-сервер (опционально)
```

| Параметр | Описание | Умолчание |
|---|---|---|
| `local_netmask` | Маска подсети локального интерфейса MSC (сигнальная сеть) | `255.255.255.248` |
| `remote_netmask` | Маска подсети удалённой стороны (HLR, BSC, SGSN и т.д.) | `255.255.255.0` |
| `gateway` | IP-адрес шлюза по умолчанию для сигнального трафика | `100.100.100.1` |
| `ntp_primary` | Основной NTP-сервер (источник синхронизации времени) | `10.86.1.100` |
| `ntp_secondary` | Резервный NTP-сервер | `192.168.33.3` |

Эти параметры **глобальны** — применяются ко всем 7 интерфейсам одновременно. Отображаются в каждом Transport-блоке вывода `--show-*`:
```
  Transport:
    LIP: 168.192.0.2  LP: 2505
    RIP: 10.10.10.2  RP: 1585
    Mask-L: 255.255.255.248   Mask-R: 255.255.255.0
    GW:     100.100.100.1
    NTP:    100.100.100.2   (резерв: 100.100.100.3)
```

---

## Global Title (GT) — маршрутизация SCCP в сетях оператора

По умолчанию SCCP-адресация работает в режиме **route-on-SSN** (GTI=0): пакет доставляется по номеру подсистемы (SSN). Это достаточно для лабораторных стендов, где MSC и HLR соединены напрямую.

В **реальных сетях оператора** между MSC и HLR обычно стоит STP (Signal Transfer Point), который маршрутизирует по **Global Title** (E.164-номер узла). Для этого нужен режим **route-on-GT** (GTI=4, ITU-T Q.713 §3.4).

### Включение GT-маршрутизации

**Шаг 1.** В `vmsc.conf` укажите E.164-адрес самого MSC:
```ini
[subscriber]
msc_gt=79031234567    # номер MSC (без '+', только цифры)
```

**Шаг 2.** В `vmsc_interfaces.conf` раскомментируйте GT-параметры нужного интерфейса:
```ini
[c-interface]         # MSC → HLR
gt_ind=4              # включить GT (0=выкл, 4=GTI-4)
gt_called=79161234567 # E.164 адрес HLR

[f-interface]         # MSC → EIR
gt_ind=4
gt_called=79161234568 # E.164 адрес EIR
```

**Шаг 3.** При необходимости скорректируйте общие GT-параметры в `[gt]`:
```ini
[gt]
tt=0    # Translation Type (0 = без трансляции)
np=1    # Numbering Plan: 1=E.164, 6=E.212 (IMSI)
nai=4   # Nature of Address: 1=subscriber, 3=national, 4=international
```

### Параметры GT

| Параметр | Секция | Описание |
|---|---|---|
| `msc_gt` | `[subscriber]` | E.164-адрес MSC — используется как Calling Party GT |
| `gt_ind` | `[c/f/e/gs-interface]` | GTI: `0`=route-on-SSN (умолч.), `4`=route-on-GT |
| `gt_called` | `[c/f/e/gs-interface]` | E.164-адрес целевого узла (HLR/EIR/MSC/SGSN) |
| `tt` | `[gt]` | Translation Type (обычно `0`) |
| `np` | `[gt]` | Numbering Plan: `1`=E.164, `6`=E.212 |
| `nai` | `[gt]` | Nature of Address Indicator: `4`=international |

### Проверка GT-конфигурации

```bash
./vmsc --show-c-interface    # покажет GT-блок:
                             #   Mode:       Route on GT (GTI=4, RI=0)
                             #   TT/NP/NAI:  0/1/4
                             #   Calling GT: 79031234567 (MSC)
                             #   Called  GT: 79161234567 (HLR)
```

При `gt_ind=0` (умолчание) вместо этого выводится:
```
  Global Title:
    (GT не настроен — маршрутизация по SSN)
```

### [identity]
Идентификаторы абонента:
- `imsi` — International Mobile Subscriber Identity

### [bssmap]
Параметры BSSMAP:
- `cell_id` — Cell Identity (используется в Complete Layer 3 Information)

### [transport]
Параметры транспорта:
- `udp_host` — IP адрес получателя
- `udp_port` — UDP порт получателя

## Комментарии в конфигурации

Строки, начинающиеся с `#` или `;`, игнорируются:

```ini
# Это комментарий
; Это тоже комментарий

[network]
mcc=250    # Комментарий после значения
```

## Редактирование конфигурации

Конфигурационный файл можно редактировать любым текстовым редактором:

```bash
nano ~/.vmsc.conf
vim ~/.vmsc.conf
code ~/.vmsc.conf
```

После редактирования изменения применяются при следующем запуске vMSC.