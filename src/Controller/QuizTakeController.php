<?php

namespace Drupal\quizz\Controller;

use Drupal\quizz\Controller\Legacy\QuizTakeLegacyController;
use Drupal\quizz\Entity\Result;
use RuntimeException;
use stdClass;

class QuizTakeController extends QuizTakeLegacyController {

  /** @var Result */
  private $result;

  /** @var stdClass */
  private $account;

  public function __construct($quiz, $account) {
    $this->quiz = $quiz;
    $this->account = $account;
  }

  public function render() {
    try {
      if (isset($this->quiz->rendered_content)) {
        return $this->quiz->rendered_content;
      }

      $this->initQuizResult();
      if ($this->getResultId()) {
        drupal_goto($this->getQuestionTakePath());
      }
    }
    catch (\RuntimeException $e) {
      return array(
          'body' => array(
              '#prefix' => '<div class="messages error">',
              '#suffix' => '</div>',
              '#markup' => $e->getMessage()
          )
      );
    }
  }

  public function initQuizResult() {
    // Inject result from user's session
    if (!empty($_SESSION['quiz'][$this->getQuizId()]['result_id'])) {
      $this->result_id = $_SESSION['quiz'][$this->getQuizId()]['result_id'];
      $this->result = quiz_result_load($this->result_id);
    }

    // Enforce that we have the same quiz version.
    if (($this->result) && ($this->quiz->vid != $this->result->quiz_vid)) {
      $this->quiz = quiz_load($this->getQuizId(), $this->quiz->vid);
    }

    // Resume quiz progress
    if (!$this->result && $this->quiz->allow_resume) {
      $this->initQuizResume();
    }

    // Start new quiz progress
    if (!$this->result) {
      if (!$this->checkAvailability()) {
        throw new RuntimeException(t('This @quiz is closed.', array('@quiz' => QUIZ_NAME)));
      }

      $this->result = quiz_controller()->getResultGenerator()->generate($this->quiz, $this->account);
      $this->result_id = $this->result->result_id;
      $_SESSION['quiz'][$this->getQuizId()]['result_id'] = $this->result->result_id;
      $_SESSION['quiz'][$this->getQuizId()]['current'] = 1;

      module_invoke_all('quiz_begin', $this->quiz, $this->result->result_id);
    }

    if (TRUE !== $this->quiz->isAvailable($this->account)) {
      throw new RuntimeException(t('This @quiz is not available.', array('@quiz' => QUIZ_NAME)));
    }
  }

  /**
   * If we allow resuming we can load it from the database.
   */
  public function initQuizResume() {
    if (!$result_id = $this->activeResultId($this->account->uid, $this->quiz->qid)) {
      return FALSE;
    }

    $_SESSION['quiz'][$this->getQuizId()]['result_id'] = $result_id;
    $_SESSION['quiz'][$this->getQuizId()]['current'] = 1;
    $this->result = quiz_result_load($result_id);
    $this->quiz = quiz_load($this->result->quiz_qid, $this->result->quiz_vid);
    $this->result_id = $result_id;

    // Resume a quiz from the database.
    drupal_set_message(t('Resuming a previous @quiz in-progress.', array('@quiz' => QUIZ_NAME)), 'status');
  }

  /**
   * Actions to take place at the start of a quiz.
   *
   * This is called when the quiz entity is viewed for the first time. It ensures
   * that the quiz can be taken at this time.
   *
   * @return
   *   Return quiz_results result_id, or FALSE if there is an error.
   */
  private function checkAvailability() {
    $user_is_admin = entity_access('update', 'quiz_entity', $this->quiz);

    // Make sure this is available.
    if ($this->quiz->quiz_always != 1) {
      // Compare current GMT time to the open and close dates (which should still
      // be in GMT time).
      $now = REQUEST_TIME;

      if ($now >= $this->quiz->quiz_close || $now < $this->quiz->quiz_open) {
        if ($user_is_admin) {
          $msg = t('You are marked as an administrator or owner for this @quiz. While you can take this @quiz, the open/close times prohibit other users from taking this @quiz.', array('@quiz' => QUIZ_NAME));
          drupal_set_message($msg, 'status');
        }
        else {
          $msg = t('This @quiz is not currently available.', array('@quiz' => QUIZ_NAME));
          drupal_set_message($msg, 'status');
          return FALSE; // Can't take quiz.
        }
      }
    }

    // Check to see if this user is allowed to take the quiz again:
    if ($this->quiz->takes > 0) {
      $taken = db_query("SELECT COUNT(*) AS takes FROM {quiz_results} WHERE uid = :uid AND quiz_qid = :qid", array(
          ':uid' => $this->account->uid,
          ':qid' => $this->getQuizId()
        ))->fetchField();
      $allowed_times = format_plural($this->quiz->takes, '1 time', '@count times');
      $taken_times = format_plural($taken, '1 time', '@count times');

      // The user has already taken this quiz.
      if ($taken) {
        if ($user_is_admin) {
          $msg = t('You have taken this @quiz already. You are marked as an owner or administrator for this quiz, so you can take this quiz as many times as you would like.', array('@quiz' => QUIZ_NAME));
          drupal_set_message($msg, 'status');
        }
        // If the user has already taken this quiz too many times, stop the user.
        elseif ($taken >= $this->quiz->takes) {
          $msg = t('You have already taken this @quiz @really. You may not take it again.', array('@quiz', QUIZ_NAME, '@really' => $taken_times));
          drupal_set_message($msg, 'error');
          return FALSE;
        }
        // If the user has taken the quiz more than once, see if we should report
        // this.
        elseif ($this->quiz->show_attempt_stats) {
          $msg = t("You can only take this @quiz @allowed. You have taken it @really.", array('@quiz' => QUIZ_NAME, '@allowed' => $allowed_times, '@really' => $taken_times));
          drupal_set_message($msg, 'status');
        }
      }
    }

    // Check to see if the user is registered, and user alredy passed this quiz.
    if ($this->quiz->show_passed && $this->account->uid && quiz()->getQuizHelper()->isPassed($this->account->uid, $this->getQuizId(), $this->quiz->vid)) {
      $msg = t('You have already passed this @quiz.', array('@quiz' => QUIZ_NAME));
      drupal_set_message($msg, 'status');
    }

    return TRUE;
  }

}
