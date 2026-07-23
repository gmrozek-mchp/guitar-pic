<!-- MPLAB MCC RULES START -->
---
description: Microcontroller Peripheral & Pin Configuration Rule
---

### Middleware Components

Middleware configuration via MCP tools is coming soon. The following require the MCC UI in VS Code:

- **Networking**: TCP/IP, HTTP, DHCP, DNS, FTP, MQTT
- **USB**: USB Device/Host, CDC, HID, MSD
- **Wireless**: WiFi, BLE, Zigbee, Thread
- **Graphics**: GFX, Legato, LVGL
- **Audio**: Codecs, I2S, decoders
- **Motor Control**: PMSM, FOC algorithms
- **Security**: Crypto, WolfSSL, TLS
- **RTOS**: FreeRTOS, ThreadX

If the user requests middleware, guide them to use the MCC UI and offer to help with underlying peripheral configuration (SERCOM, timers, pins, clocks).

#### Tool Execution

Strictly execute MCP tools in sequence one after the other.

#### MCC Launch and Timeout Handling

- **MCC takes several minutes to launch on first use.** If a tool call times out or returns a connection error, this is expected behavior during MCC startup.
- **NEVER fall back to "general knowledge" or manual code writing** when MCC tools timeout.
- **NEVER write peripheral initialization code manually** - always use MCC tools to configure peripherals and generate code.
- Retry the tool call up to 2 times. If it still fails after 3 attempts, inform the user that MCC may still be launching and ask them to wait before retrying.

#### 0. Mandatory Tool Workflow (NEVER skip these steps)

- **NEVER guess or hallucinate component names or symbol names.**
- Before adding any component, call `mcc_config_get_components` to discover valid component names.
- Before setting any symbol value, call `mcc_config_get_symbols_info` for that component to retrieve exact symbol names, types, and constraints.
- Only use component names and symbol names returned by these tools - do not invent or guess names.

#### 1. Peripheral/Component Analysis

- Analyze the user request to identify all required microcontroller peripherals and components.

#### 2. Resource Availability Verification

- Before making any configuration changes, verify every identified peripheral/component exists on the target device.
- Ensure sufficient instances of each resource are available.
- If any required resource is unavailable:
  - Clearly state which resource is missing.
  - Explain why that resource is required.
  - Stop immediately without making any configuration changes.

#### 3. Component Addition & Symbol Management

- For each identified and available peripheral/component:
  - Add the component to the project.
  - Read and respect symbol metadata: type, allowed values, constraints.
  - Read the initialization section of that particular component from datasheet for better understanding.
  - Update symbol values strictly within metadata constraints and according to explicit/inferred user intent:
    - Boolean symbols: Only accept boolean values.
    - Integer symbols: Respect defined minimum and maximum values.
    - Combo/Menu symbols: Restrict to enumerated allowed values.
    - String symbols: Accept only valid string values, not numeric values.

#### 4. Pin Configuration Rules

- If configuration involves any pin:
  - Treat the port component as the sole authority for pin configuration changes.
  - **CRITICAL**: Call `mcc_config_get_symbols_info` for 'port' component and read the returned `symbolSchema` carefully.
  - Follow the exact pattern shown in the schema - do not guess or invent symbol names.
  - Do NOT modify any other pin-related properties, including:
    - Electrical characteristics
    - Pull configuration
    - Drive strength
    - Peripheral-level pin selection
  - Must NOT introduce additional pin multiplexing changes or conflicts.

#### 5. Clock Configuration Rules

- All clock related configurations like GCLK, Oscillator Control etc are available as part of 'clock' component.

#### 6. Consistency & Constraint Enforcement

- Validate overall configuration consistency across all components and pins.
- Ensure unavailable resources are not configured.
- Ensure symbol metadata and constraints are not violated.
- Must NOT proceed with configuration after a blocking failure.

### 7. Evaluation Kit (EVK) Reference Requirement

- If the user request references, implies, or explicitly mentions an EVK (Evaluation Kit):
- The EVK User Guide must be consulted before any analysis or configuration.
- Use the EVK User Guide to:
- Identify the exact EVK model and supported target device(s).
- Verify available peripherals, pin mappings, onboard components, and hardware limitations.
- Confirm any fixed or pre-routed pin assignments imposed by the EVK.
- All peripheral selection, pin configuration, and resource validation must align with the EVK User Guide.
- If the EVK User Guide does not support the requested peripheral, pin usage, or configuration:
- Clearly state the conflict and its source (EVK limitation or fixed routing).
- Explain why the requested configuration cannot be satisfied.
- Stop immediately without making any configuration changes.

<!-- MPLAB MCC RULES END -->
