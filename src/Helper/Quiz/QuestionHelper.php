<?php

namespace Drupal\quiz\Helper\Quiz;

class QuestionHelper {

  /**
   * Update the session for this quiz to the active question.
   *
   * @param type $quiz
   *   A Quiz node.
   * @param type $question_number
   *   Question number starting at 1.
   */
  public function redirect($quiz, $question_number) {
    $_SESSION['quiz'][$quiz->nid]['current'] = $question_number;
  }

}