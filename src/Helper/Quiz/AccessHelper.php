<?php

namespace Drupal\quiz\Helper\Quiz;

use Drupal\quiz\Controller\QuizTakeController;
use Drupal\quiz\Entity\QuizEntity;
use Drupal\quiz\Quiz;
use stdClass;

/**
 * Helper class to provide methods to check user access right to quiz,
 * questions, feedback, score, …
 */
class AccessHelper {

  public function userHasResult($quiz, $uid) {
    $sql = 'SELECT 1 FROM {quiz_results} WHERE quiz_qid = :qid AND uid = :uid AND is_evaluated = :is_evaluated';
    return db_query($sql, array(':qid' => $quiz->qid, ':uid' => $uid, ':is_evaluated' => 1))
        ->fetchField();
  }

  /**
   * Helper function to determine if a user has access to view his quiz results
   *
   * @param object $quiz
   *  The Quiz entity
   */
  public function canAccessMyResults($quiz, $account) {
    if ($quiz->type !== 'quiz') {
      return false;
    }
    return $this->userHasResult($quiz, $account->uid);
  }

  /**
   * Helper function to determine if a user has access to the different results
   * pages.
   *
   * @param $quiz
   *   The quiz entity.
   * @param $result_id
   *   The result id of a result we are trying to access.
   * @return boolean
   *   TRUE if user has permission.
   */
  public function canAccessResults($account, $quiz, $result_id = NULL) {
    if ($quiz->type !== 'quiz') {
      return FALSE;
    }
    // If rid is set we must make sure the result belongs to the quiz we are
    // viewing results for.
    if (isset($result_id)) {
      $result = db_query('SELECT qnr.quiz_qid, qnr.uid '
        . ' FROM {quiz_results} qnr '
        . ' WHERE result_id = :result_id', array(':result_id' => $result_id)
        )->fetch();
      if ($result && $result->quiz_qid != $quiz->qid) {
        return FALSE;
      }
    }

    if (user_access('view any quiz results')) {
      return TRUE;
    }

    if (user_access('view results for own quiz') && $account->uid == $quiz->uid) {
      return TRUE;
    }

    if (user_access('score taken quiz answer')) {
      // check if the taken user is seeing his result
      if (isset($result_id) && $result && $result->uid == $account->uid) {
        return TRUE;
      }
    }
  }

  /**
   * Helper function to determine if a user has access to score a quiz.
   *
   * @param $quiz_creator
   *   uid of the quiz creator.
   */
  public function canAccessQuizScore($account, $quiz_creator = NULL) {
    if ($quiz_creator == NULL && ($quiz = $this->getQuizFromMenu())) {
      $quiz_creator = $quiz->uid;
    }
    if (user_access('score any quiz')) {
      return TRUE;
    }
    if (user_access('score own quiz') && $account->uid == $quiz_creator) {
      return TRUE;
    }
    if (user_access('score taken quiz answer')) {
      return TRUE;
    }
  }

  /**
   * Retrieves the quiz entity from the menu router.
   *
   * @return
   *   Quiz entity, if found, or FALSE if quiz entity can't be retrieved from the menu
   *   router.
   */
  private function getQuizFromMenu() {
    if ($to_return = menu_get_object('quiz_type_access', 4)) {
      return $to_return;
    }

    // @TODO: FIX it. This seems to return NULL in feedback page.
    $node = menu_get_object();
    return (is_object($node) && $node->type == 'quiz') ? $node : FALSE;
  }

  /**
   *
   * @global stdClass $user
   * @param QuizEntity $quiz
   * @param int $page_number
   * @return boolean
   */
  public function canAccessQuestion($quiz, $page_number) {
    global $user;

    if (!$page_number) {
      return FALSE;
    }

    // User maybe revisiting the quiz, trying to resume.
    if (!isset($_SESSION['quiz'][$quiz->qid]) && !user_is_anonymous()) {
      $controller = new QuizTakeController($quiz, $user);
      if (FALSE === $controller->initQuizResume()) {
        return FALSE;
      }
    }

    // Access to go to any question.
    if ($quiz->allow_jumping) {
      return TRUE;
    }

    $result_id = (int) $_SESSION['quiz'][$quiz->qid]['result_id'];
    $result = quiz_result_load($result_id);
    $question_index = $page_number;
    $qinfo_last = $page_number == 1 ? NULL : $result->layout[$question_index - 1];
    $qinfo = $result->layout[$question_index];
    $question = quiz_question_entity_load($qinfo['nid'], $qinfo['vid']);
    $question_last = quiz_question_entity_load($qinfo_last['nid'], $qinfo_last['vid']);

    // No backwards navigation & Already have an answer for the requested question.
    if (!$quiz->backwards_navigation && quiz_result_is_question_answered($result, $question)) {
      return FALSE;
    }

    // this is the first question.
    if (1 == $page_number) {
      return TRUE;
    }

    // Enforce normal navigation. Previous answer was submitted.
    if (!quiz_result_is_question_answered($result, $question_last)) {
      return FALSE;
    }

    return TRUE;
  }

  public function canTakeQuiz($quiz, $account) {
    if (!quiz()->getQuizHelper()->isAvailable($quiz)) {
      return FALSE;
    }
    return entity_access('view', 'quiz_entity', $quiz, $account) && user_access('access quiz', $account);
  }

}
