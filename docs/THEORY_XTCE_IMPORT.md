# Theory: XTCE to Concordia Converter

## The "Meme" Idea
Converting XTCE (XML Telemetric and Command Exchange) to Concordia (`.cnd`) is not just a meme—it's a viable migration path. XTCE is the industry standard (NASA, ESA, CCSDS), but it is verbose and difficult to write by hand. Concordia is concise.

A CLI tool `cnd import-xtce <file.xml>` could automate 90% of the work.

## Mapping Strategy

### 1. Containers -> Packets/Structs
XTCE uses `SequenceContainer` to define packets.
*   **Root Container**: Becomes a `packet` in Concordia.
*   **Sub-Containers**: Become `struct` definitions.
*   **Inheritance (`BaseContainer`)**: XTCE often uses inheritance for headers (e.g., "CCSDS Packet" -> "My Packet").
    *   *Concordia Strategy*: Flatten the fields. If "My Packet" inherits "Header", the converter emits the Header fields first, then the specific fields.

### 2. Parameters -> Fields
| XTCE Element | XTCE Attribute | Concordia Equivalent |
| :--- | :--- | :--- |
| `IntegerParameterType` | `sizeInBits="8"` | `uint8` |
| `IntegerParameterType` | `sizeInBits="3"` | `uint8 : 3` (Bitfield) |
| `FloatParameterType` | `sizeInBits="32"` | `float` |
| `BooleanParameterType` | - | `bool` |
| `StringParameterType` | `FixedLength` | `string` (with max len) |
| `EnumeratedParameterType` | - | `enum` |

### 3. Encodings -> Decorators
| XTCE Encoding | Concordia Decorator |
| :--- | :--- |
| `byteOrder="mostSignificantByteFirst"` | `@big_endian` |
| `encoding="twosComplement"` | `intX` (Signed types) |
| `encoding="unsigned"` | `uintX` (Unsigned types) |

### 4. Calibrators -> Transforms
XTCE `PolynomialCalibrator` is the most common.
*   **Linear ($y = mx + c$)**:
    *   `Term exponent="1"` -> `@scale(m)`
    *   `Term exponent="0"` -> `@offset(c)`
*   **Non-Linear**:
    *   Currently unsupported (Requires `@poly` feature).
    *   *Strategy*: Emit a warning comment: `// TODO: Complex polynomial not supported`.

## Example Conversion

**Input (XTCE)**
```xml
<SequenceContainer name="BatteryStatus">
    <EntryList>
        <ParameterRefEntry parameterRef="Voltage">
            <IntegerDataEncoding sizeInBits="8" encoding="unsigned"/>
        </ParameterRefEntry>
    </EntryList>
</SequenceContainer>
<Parameter name="Voltage">
    <IntegerParameterType>
        <DefaultCalibrator>
            <PolynomialCalibrator>
                <Term exponent="1" coefficient="0.1"/>
            </PolynomialCalibrator>
        </DefaultCalibrator>
    </IntegerParameterType>
</Parameter>
```

**Output (Concordia)**
```concordia
packet BatteryStatus {
    @scale(0.1)
    uint8 voltage;
}
```

## Challenges
1.  **Conditional Inclusion**: XTCE `IncludeCondition` can be complex. Simple equality checks map to Concordia `switch` or `if`. Complex boolean logic (`A && (B || C)`) might require manual intervention.
2.  **Split Encodings**: XTCE allows a parameter to be split across non-contiguous bits (e.g., 2 bits here, 6 bits there).
    *   *Concordia Limitation*: Fields must be contiguous.
    *   *Workaround*: Define two separate fields in Concordia (e.g., `val_part1`, `val_part2`) and rely on the application layer to recombine them. This is safer and more explicit than trying to hide the split in the schema.
