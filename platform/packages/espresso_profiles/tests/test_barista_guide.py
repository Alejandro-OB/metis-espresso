"""Lo que se le dice al barista no se extrapola fuera de lo medido."""

import pytest

from espresso_profiles.barista_guide import (
    ESTIMATED,
    MEASURED,
    NO_DATA,
    cup_mass,
    machine_facts,
    time_for_ratio,
)


def test_mass_follows_the_e3_model_with_its_uncertainty():
    result = cup_mass(20.0, 13.0)
    assert result["status"] == ESTIMATED
    # 2,084·(20 − 10,03) + 1,79 ≈ 22,6 g
    assert result["massG"] == pytest.approx(22.6, abs=0.1)
    assert result["uncertaintyG"] == pytest.approx(13.2, abs=0.1)


def test_time_for_ratio_is_the_inverse_of_the_mass():
    timed = time_for_ratio(2.0, 13.0)
    assert timed["status"] == ESTIMATED
    assert cup_mass(timed["cutSeconds"], 13.0)["massG"] == pytest.approx(26.0, abs=0.2)


@pytest.mark.parametrize("call", [
    lambda: cup_mass(30.0, 13.0),
    lambda: cup_mass(20.0, 18.0),
    lambda: time_for_ratio(5.0, 13.0),
    lambda: time_for_ratio(2.0, 18.0),
])
def test_outside_what_was_measured_there_is_no_data(call):
    result = call()
    assert result["status"] == NO_DATA
    assert result["reason"]


def test_machine_facts_are_measured_and_cite_their_source():
    facts = machine_facts()
    assert {fact["status"] for fact in facts} == {MEASURED}
    assert all(fact["source"].endswith(".md") for fact in facts)
