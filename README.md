# RP2040 freertos with OLED1

# Sistema Embarcado para Arcade Stick

Este projeto implementa um sistema embarcado para um arcade stick, com suporte a botões e controles analógicos. O sistema é projetado para ser usado com um PC ou console, fornecendo feedback através de LEDs.

## Diagrama de Fluxo

O sistema embarcado segue o fluxo descrito no diagrama abaixo:

![Diagrama de Fluxo](img/ArcadeStick.png)

### 1. **Botões do Arcade**
   - Cada botão no arcade é monitorado por uma tarefa chamada `button_task`. Quando um botão é pressionado ou liberado, a informação é colocada em uma fila (`xQueueInput`).

### 2. **Controle Analógico X/Y**
   - O controle analógico (X/Y) é monitorado por outra tarefa chamada `analog_task`. Quando o analógico é movido, os dados de posição são enviados para uma fila (`xQueueAnalog`).

### 3. **Tarefa USB (`usb_task`)**
   - Ambas as filas, `xQueueInput` (botões) e `xQueueAnalog` (controle analógico), são lidas pela tarefa `usb_task`, que processa os dados recebidos e os converte em pacotes HID (Human Interface Device).

### 4. **USB HID**
   - O pacote HID gerado é enviado via USB para o PC ou console. O sistema utiliza o protocolo USB HID para enviar dados de entrada de botões e controles analógicos.

### 5. **PC/Console**
   - O PC ou console recebe os dados e os interpreta como entradas de um joystick ou controle, permitindo ao usuário interagir com o sistema.

### 6. **Feedback com LEDs/Buzzer (Opcional)**
   - Feedback visual e sonoro pode ser adicionado, como LEDs piscando ou buzzer emitindo sons, para confirmar as ações ou informar o estado do dispositivo.

## Requisitos
- Microcontrolador compatível com FreeRTOS.
- Suporte a USB HID (Human Interface Device).
- Biblioteca para controle de LEDs e buzzer (se desejado).
- Conexão USB ao PC ou Console.

### Explicações adicionais:
- **Sensores adicionar:** É extremamente possivel que suporte a outros tipos de sensores e funcionalidades sejam adicionados, isso é apenas o basico.

