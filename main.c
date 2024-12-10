/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the CANFD example
*
* Related Document: See README.md
*
*******************************************************************************
*******************************************************************************
* Copyright 2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/


/*******************************************************************************
* Header Files
*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* CAN-FD message identifier 1*/
#define CANFD_NODE_1            1
/* CAN-FD message identifier 2 (use different for 2nd device) */
#define CANFD_NODE_2            2
/* message Identifier used for this code */
#define USE_CANFD_NODE          CANFD_NODE_1
/* CAN-FD channel number used */

#if defined (CY_DEVICE_PSC3)
#define CANFD_HW_CHANNEL        1
#else
#define CANFD_HW_CHANNEL        0
#endif
/* CAN-FD data buffer index to send data from */
#define CANFD_BUFFER_INDEX      0
/* Maximum incoming data length supported */
#define CANFD_DLC               8

#if defined (CY_DEVICE_PSC3)
#define CANFD_INTERRUPT         canfd_0_interrupts0_1_IRQn
#else
#define CANFD_INTERRUPT         canfd_0_interrupts0_0_IRQn
#endif

#define GPIO_INTERRUPT_PRIORITY (7u)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* This is a shared context structure, unique for each can-fd channel */
static cy_stc_canfd_context_t canfd_context;

/* Variable which holds the button pressed status */
volatile bool gpio_intr_flag = false;

/* For the Retarget -IO (Debug UART) usage */
static cy_stc_scb_uart_context_t    DEBUG_UART_context;           /** UART context */
static mtb_hal_uart_t               DEBUG_UART_hal_obj;           /** Debug UART HAL object  */


/*******************************************************************************
* Function Prototypes
*******************************************************************************/

/* can-fd interrupt handler */
static void isr_canfd (void);

/* button press interrupt handler */
void gpio_interrupt_handler(void);

void canfd_rx_callback (bool  msg_valid, uint8_t msg_buf_fifo_num,
                        cy_stc_canfd_rx_buffer_t* canfd_rx_buf);
/* handler for general errors */
void handle_error(uint32_t status);

/*******************************************************************************
* Function Definitions
*******************************************************************************/
cy_stc_sysint_t intrCfg =
{
    .intrSrc = ioss_interrupts_sec_gpio_5_IRQn, /* Interrupt source is GPIO port 5 interrupt */
    .intrPriority = 2UL                         /* Interrupt priority is 2 */
};

/* Populate the configuration structure for CAN-FD Interrupt */
cy_stc_sysint_t canfd_irq_cfg =
{
    /* Source of interrupt signal */
    .intrSrc = CANFD_INTERRUPT,
    /* Interrupt priority */
    .intrPriority = 1U,
};
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function. It initializes the CAN-FD channel and interrupt.
* User button and User LED are also initialized. The main loop checks for the
* button pressed interrupt flag and when it is set, a CAN-FD frame is sent.
* Whenever a CAN-FD frame is received from other nodes, the user LED toggles and
* the received data is logged over serial terminal.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    cy_en_canfd_status_t status;
    /* Initialize the device and board peripherals */
    result = cybsp_init();
    /* Board init failed. Stop program execution */
    handle_error(result);
    /* Initialize retarget-io to use the debug UART port */
     result = (cy_rslt_t)Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
     /* UART init failed. Stop program execution */
     if (result != CY_RSLT_SUCCESS)
     {
          CY_ASSERT(0);
     }

     Cy_SCB_UART_Enable(DEBUG_UART_HW);

     /* Setup the HAL UART */
     result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config, &DEBUG_UART_context, NULL);

     /* HAL UART init failed. Stop program execution */
     if (result != CY_RSLT_SUCCESS)
     {
          CY_ASSERT(0);
     }

     result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

     /* HAL retarget_io init failed. Stop program execution */
     if (result != CY_RSLT_SUCCESS)
     {
          CY_ASSERT(0);
     }

     /* Configure GPIO interrupt */
     Cy_GPIO_SetInterruptEdge(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN, CY_GPIO_INTR_FALLING);
     Cy_GPIO_SetInterruptMask(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN, CY_GPIO_INTR_EN_MASK);

    /* Initialize CAN-FD Channel */
    status = Cy_CANFD_Init(CANFD_HW, CANFD_HW_CHANNEL, &CANFD_config,
                           &canfd_context);

    handle_error(status);

     /* Configure CM4+ CPU GPIO interrupt vector for Port 0 */
     Cy_SysInt_Init(&intrCfg, gpio_interrupt_handler);
     NVIC_ClearPendingIRQ(intrCfg.intrSrc);
     NVIC_EnableIRQ((IRQn_Type)intrCfg.intrSrc);
    /* Hook the interrupt service routine */
    (void) Cy_SysInt_Init(&canfd_irq_cfg, &isr_canfd);
    /* enable the CAN-FD interrupt */
    NVIC_EnableIRQ(CANFD_INTERRUPT);
    /* Enable global interrupts */
    __enable_irq();

    printf("===========================================================\r\n");
    printf("Welcome to CAN-FD example\r\n");
    printf("===========================================================\r\n\n");

    printf("===========================================================\r\n");
    printf("CAN-FD Node-%d (message id)\r\n", USE_CANFD_NODE);
    printf("===========================================================\r\n\n");

    /* Setting Node(message) Identifier to global setting of "USE_CANFD_NODE" */
    CANFD_T0RegisterBuffer_0.id = USE_CANFD_NODE;

    for(;;)
    {
        if (true == gpio_intr_flag)
        {
            /* Sending CAN-FD frame to other node */
            status = Cy_CANFD_UpdateAndTransmitMsgBuffer(CANFD_HW,
                                                    CANFD_HW_CHANNEL,
                                                    &CANFD_txBuffer_0,
                                                    CANFD_BUFFER_INDEX,
                                                    &canfd_context);
            if(CY_CANFD_SUCCESS == status)
            {
                printf("CAN-FD Frame sent with message ID-%d\r\n\r\n",
                        USE_CANFD_NODE);
            }
            else
            {
                printf("Error sending CAN-FD Frame with message ID-%d\r\n\r\n",
                        USE_CANFD_NODE);
            }

            gpio_intr_flag = false;
        }
    }
}

/*******************************************************************************
* Function Name: gpio_interrupt_handler
********************************************************************************
* Summary:
*   GPIO interrupt handler.
*
* Parameters:
*  void *handler_arg (unused)
*  cyhal_gpio_event_t (unused)
*
*******************************************************************************/
void gpio_interrupt_handler(void)
{
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    gpio_intr_flag = true;
}

/*******************************************************************************
* Function Name: isr_canfd
********************************************************************************
* Summary:
* This is the interrupt handler function for the can-fd interrupt.
*
* Parameters:
*  none
*
*******************************************************************************/
static void isr_canfd(void)
{
    /* Just call the IRQ handler with the current channel number and context */
    Cy_CANFD_IrqHandler(CANFD_HW, CANFD_HW_CHANNEL, &canfd_context);
}

/*******************************************************************************
* Function Name: canfd_rx_callback
********************************************************************************
* Summary:
* This is the callback function for can-fd reception
*
* Parameters:
*    msg_valid                     Message received properly or not
*    msg_buf_fifo_num              RxFIFO number of the received message
*    canfd_rx_buf                  Message buffer
*
*******************************************************************************/
void canfd_rx_callback (bool  msg_valid, uint8_t msg_buf_fifo_num,
                        cy_stc_canfd_rx_buffer_t* canfd_rx_buf)
{
    /* Array to hold the data bytes of the CAN-FD frame */
    uint8_t canfd_data_buffer[CANFD_DLC];
    /* Variable to hold the data length code of the CAN-FD frame */
    uint32_t canfd_dlc;
    /* Variable to hold the Identifier of the CAN-FD frame */
    uint32_t canfd_id;

    if (true == msg_valid)
    {
        /* Checking whether the frame received is a data frame */
        if(CY_CANFD_RTR_DATA_FRAME == canfd_rx_buf->r0_f->rtr)
        {

            //cyhal_gpio_toggle(CYBSP_USER_LED);
             Cy_GPIO_Inv(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN);

            canfd_dlc = canfd_rx_buf->r1_f->dlc;
            canfd_id  = canfd_rx_buf->r0_f->id;

            printf("%d bytes received with message identifier %d\r\n\r\n",
                                                        (int)canfd_dlc,
                                                        (int)canfd_id);

            memcpy(canfd_data_buffer,canfd_rx_buf->data_area_f,canfd_dlc);

            printf("Rx Data : ");

            for (uint8_t msg_idx = 0U; msg_idx < canfd_dlc ; msg_idx++)
            {
                printf(" %d ", canfd_data_buffer[msg_idx]);
            }

            printf("\r\n\r\n");
        }
    }
}

/*******************************************************************************
* Function Name: handle_error
********************************************************************************
*
* Summary:
* User defined error handling function. This function processes unrecoverable
* errors such as any initialization errors etc. In case of such error the system
* will enter into assert.
*
* Parameters:
*  uint32_t status - status indicates success or failure
*
* Return:
*  void
*
*******************************************************************************/
void handle_error(uint32_t status)
{
    if (status != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
}

/* [] END OF FILE */
