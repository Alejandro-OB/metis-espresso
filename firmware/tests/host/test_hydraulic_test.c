/*
 * Verificación en host del ensayo hidráulico E2 con las actas aprobadas
 * (enmienda a ADR-007 § 5 del 2026-10-16).
 *
 * Comprueba lo que sostiene el módulo: solo las órdenes declaradas y en
 * cualquier orden, nada por encima del techo del acta, la orden se fija al
 * abrir la extracción y cada corrida exige otra, y sin orden la bomba queda
 * en cero.
 */

#include <assert.h>
#include <stdio.h>

#include "hydraulic_test.h"

static const uint8_t ORDERS[HYDRAULIC_TEST_ORDERS] = {5U, 20U, 75U};

static void test_parses_only_its_own_lines(void)
{
    assert(hydraulic_test_parse("B,step,20").kind == HYDRAULIC_TEST_COMMAND_NONE);
    assert(hydraulic_test_parse("H,show\n").kind == HYDRAULIC_TEST_COMMAND_SHOW);
    const hydraulic_test_command_t order = hydraulic_test_parse("H,order,20\r\n");
    assert(order.kind == HYDRAULIC_TEST_COMMAND_ORDER && order.percent == 20U);
    assert(hydraulic_test_parse("H,order,20%").kind == HYDRAULIC_TEST_COMMAND_INVALID);
    assert(hydraulic_test_parse("H,order,").kind == HYDRAULIC_TEST_COMMAND_INVALID);
    assert(hydraulic_test_parse("H,order,150").kind == HYDRAULIC_TEST_COMMAND_INVALID);
    assert(hydraulic_test_parse("H,step,20").kind == HYDRAULIC_TEST_COMMAND_INVALID);
}

static void test_rejects_orders_it_cannot_honour(void)
{
    hydraulic_test_t test;
    const uint8_t over[HYDRAULIC_TEST_ORDERS] = {5U, 20U, 80U};
    const uint8_t zero[HYDRAULIC_TEST_ORDERS] = {0U, 20U, 75U};
    const uint8_t repeated[HYDRAULIC_TEST_ORDERS] = {20U, 20U, 75U};
    assert(!hydraulic_test_configure(&test, over, 75.0F));
    assert(!test.configured);
    assert(!hydraulic_test_configure(&test, zero, 75.0F));
    assert(!hydraulic_test_configure(&test, repeated, 75.0F));
    assert(!hydraulic_test_configure(&test, ORDERS, 0.0F));
    /* Sin configurar no se elige nada ni se mueve la bomba. */
    assert(hydraulic_test_select(&test, 20U, OP_STATE_READY) ==
           HYDRAULIC_TEST_REJECTED_NOT_CONFIGURED);
    assert(hydraulic_test_step(&test, OP_STATE_EXTRACTION) == 0U);
}

static void test_any_declared_order_in_any_order(void)
{
    hydraulic_test_t test;
    assert(hydraulic_test_configure(&test, ORDERS, 75.0F));
    assert(hydraulic_test_select(&test, 75U, OP_STATE_READY) == HYDRAULIC_TEST_ACCEPTED);
    assert(hydraulic_test_select(&test, 5U, OP_STATE_READY) == HYDRAULIC_TEST_ACCEPTED);
    assert(hydraulic_test_select(&test, 5U, OP_STATE_COFFEE_HEATING) == HYDRAULIC_TEST_ACCEPTED);
    assert(hydraulic_test_select(&test, 30U, OP_STATE_READY) ==
           HYDRAULIC_TEST_REJECTED_NOT_DECLARED);
    assert(test.selected == 5U);
}

static void test_no_order_means_no_pump(void)
{
    hydraulic_test_t test;
    assert(hydraulic_test_configure(&test, ORDERS, 75.0F));
    assert(hydraulic_test_step(&test, OP_STATE_READY) == 0U);
    assert(hydraulic_test_step(&test, OP_STATE_EXTRACTION) == 0U);
}

static void test_order_latched_per_extraction(void)
{
    hydraulic_test_t test;
    assert(hydraulic_test_configure(&test, ORDERS, 75.0F));
    assert(hydraulic_test_select(&test, 20U, OP_STATE_READY) == HYDRAULIC_TEST_ACCEPTED);
    assert(hydraulic_test_step(&test, OP_STATE_READY) == 0U);
    assert(hydraulic_test_step(&test, OP_STATE_EXTRACTION) == 20U);
    /* Con la bomba en marcha no se cambia, y la corrida sigue con la suya. */
    assert(hydraulic_test_select(&test, 75U, OP_STATE_EXTRACTION) ==
           HYDRAULIC_TEST_REJECTED_PUMP_RUNNING);
    assert(hydraulic_test_step(&test, OP_STATE_EXTRACTION) == 20U);
    /* El purgado no hereda la orden del ensayo. */
    assert(hydraulic_test_step(&test, OP_STATE_COFFEE_HEATING) == 0U);
    assert(hydraulic_test_step(&test, OP_STATE_MANUAL_PURGE) == 0U);
    assert(hydraulic_test_select(&test, 75U, OP_STATE_READY) == HYDRAULIC_TEST_ACCEPTED);
    assert(hydraulic_test_step(&test, OP_STATE_EXTRACTION) == 75U);
    /* La elegida se queda para la siguiente: repetir no exige volver a pedirla. */
    assert(hydraulic_test_step(&test, OP_STATE_READY) == 0U);
    assert(hydraulic_test_step(&test, OP_STATE_EXTRACTION) == 75U);
}

int main(void)
{
    test_parses_only_its_own_lines();
    test_rejects_orders_it_cannot_honour();
    test_any_declared_order_in_any_order();
    test_no_order_means_no_pump();
    test_order_latched_per_extraction();
    puts("hydraulic_test: ok");
    return 0;
}
