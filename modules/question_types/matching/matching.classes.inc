<?php

use Drupal\quiz_question\QuestionPlugin;
use Drupal\quiz_question\QuizQuestionResponse;

/**
 * matching.classes
 *
 * This module uses the question interface to define the Matching question type.
 *
 * A Matching node defines a series of questions and answers and requires the user
 * to associate each answer with a question.
 *
 * @file
 */

/**
 * Extension of QuizQuestion.
 */
class MatchingQuestion extends QuestionPlugin {

  /**
   * Constructor
   *
   * @param $question
   *  matching node
   */
  public function __construct(\Drupal\quiz_question\Entity\Question $question) {
    parent::__construct($question);
    if (empty($this->question->match)) {
      $this->question->match = array();
    }
  }

  /**
   * Implementation of saveEntityProperties
   *
   * @see QuizQuestion#saveEntityProperties($is_new)
   */
  public function saveEntityProperties($is_new = FALSE) {
    // Update or insert the question properties
    db_merge('quiz_matching_properties')
      ->key(array('qid' => $this->question->qid, 'vid' => $this->question->vid))
      ->fields(array('choice_penalty' => $this->question->choice_penalty))
      ->execute();

    // Loop through each question-answer combination
    foreach ($this->question->match as $match) {
      $match['feedback'] = (isset($match['feedback'])) ? $match['feedback'] : '';
      // match_id is not so it is a new question.
      if ($is_new || empty($match['match_id']) || $this->question->revision || isset($this->question->node_export_drupal_version)) {
        if (!empty($match['question']) && !empty($match['answer'])) {
          db_insert('quiz_matching_question')
            ->fields(array(
                'qid'      => $this->question->qid,
                'vid'      => $this->question->vid,
                'question' => $match['question'],
                'answer'   => $match['answer'],
                'feedback' => $match['feedback'],
            ))
            ->execute();
        }
      }
      // match_id is set, user may remove or update existing question.
      else {
        if (empty($match['question']) && empty($match['answer'])) {
          // remove sub question.
          db_delete('quiz_matching_question')
            ->condition('match_id', $match['match_id'])
            ->execute();
        }
        else {
          // update sub question.
          db_update('quiz_matching_question')
            ->fields(array(
                'question' => $match['question'],
                'answer'   => $match['answer'],
                'feedback' => $match['feedback'],
            ))
            ->condition('match_id', $match['match_id'])
            ->execute();
        }
      }
    }
  }

  /**
   * Implementation of validateNode
   *
   * @see QuizQuestion#validateNode($form)
   */
  public function validate(array &$form) {
    // No validation is required
    // The first two pairs are required in the form, if there are other errors we forgive them
  }

  /**
   * Implementation of delete
   *
   * @see QuizQuestion#delete($only_this_version)
   */
  public function delete($only_this_version = FALSE) {
    parent::delete($only_this_version);

    $delete_properties = db_delete('quiz_matching_properties')->condition('qid', $this->question->qid);

    if ($only_this_version) {
      $delete_properties->condition('vid', $this->question->vid);

      $match_id = db_query('SELECT match_id FROM {quiz_matching_question} WHERE qid = :qid AND vid = :vid', array(
          ':qid' => $this->question->qid,
          ':vid' => $this->question->vid))->fetchCol();
      db_delete('quiz_matching_user_answers')
        ->condition('match_id', is_array($match_id) ? $match_id : array(0))
        ->execute();

      db_delete('quiz_matching_question')
        ->condition('qid', $this->question->qid)
        ->condition('vid', $this->question->vid)
        ->execute();
    }
    // Delete all versions of this question.
    else {
      $match_id = db_query(
        'SELECT match_id FROM {quiz_matching_question} WHERE qid = :qid', array(':qid' => $this->question->qid))->fetchCol();
      if (!empty($match_id)) {
        db_delete('quiz_matching_user_answers')
          ->condition('match_id', $match_id)
          ->execute();
      }

      db_delete('quiz_matching_question')
        ->condition('qid', $this->question->qid)
        ->execute();
    }

    $delete_properties->execute();
  }

  /**
   * Implementation of getNodeProperties
   *
   * @see QuizQuestion#getNodeProperties()
   */
  public function load() {
    if (isset($this->entityProperties)) {
      return $this->entityProperties;
    }
    $props = parent::load();

    $res_a = db_query(
      'SELECT choice_penalty FROM {quiz_matching_properties} WHERE qid = :qid AND vid = :vid', array(
        ':qid' => $this->question->qid,
        ':vid' => $this->question->vid
      ))->fetchAssoc();

    if (is_array($res_a)) {
      $props = array_merge($props, $res_a);
    }

    $query = db_query('SELECT match_id, question, answer, feedback FROM {quiz_matching_question} WHERE qid = :qid AND vid = :vid', array(
        ':qid' => $this->question->qid,
        ':vid' => $this->question->vid
    ));
    while ($result = $query->fetch()) {
      $props['match'][] = array(
          'match_id' => $result->match_id,
          'question' => $result->question,
          'answer'   => $result->answer,
          'feedback' => $result->feedback,
      );
    }
    $this->entityProperties = $props;
    return $props;
  }

  /**
   * Implementation of getEntityView
   *
   * @see QuizQuestion#getEntityView()
   */
  public function getEntityView() {
    $content = parent::getEntityView();

    list($matches, $select_option) = $this->getSubquestions();
    $subquestions = array();
    if ($this->viewCanRevealCorrect()) {
      foreach ($matches as $match) {
        $subquestions[] = array(
            'question' => $match['question'],
            'correct'  => $match['answer'],
            'feedback' => $match['feedback']
        );
      }
    }
    else {
      // shuffle the answer column
      foreach ($matches as $match) {
        $sub_qs[] = $match['question'];
        $sub_as[] = $match['answer'];
      }
      shuffle($sub_as);
      foreach ($sub_qs as $i => $sub_q) {
        $subquestions[] = array(
            'question' => $sub_q,
            'random'   => $sub_as[$i],
        );
      }
    }
    $content['answers'] = array(
        '#markup' => theme('matching_match_node_view', array('subquestions' => $subquestions)),
        '#weight' => 2,
    );
    return $content;
  }

  /**
   * Implementation of getAnsweringForm
   *
   * @see QuizQuestion#getAnsweringForm($form_state, $result_id)
   */
  public function getAnsweringForm(array $form_state = NULL, $result_id) {
    $form = parent::getAnsweringForm($form_state, $result_id);

    if (isset($result_id)) {
      // The question has already been answered. We load the answers
      $response = new MatchingResponse($result_id, $this->question);
      $responses = $response->getResponse();
    }
    list($matches, $select_option) = $this->getSubquestions();
    //$form['#theme'] = 'matching_subquestion_form';
    foreach ($matches as $match) {
      $form[$match['match_id']] = array(
          '#title'   => $match['question'],
          '#type'    => 'select',
          '#options' => $this->customShuffle($select_option),
      );
      if ($responses) {
        // If this question already has been answered
        $form[$match['match_id']]['#default_value'] = $responses[$match['match_id']];
      }
    }
    if (variable_get('quiz_matching_shuffle_options', TRUE)) {
      $form = $this->customShuffle($form);
    }
    $form['scoring_info'] = array(
        '#markup' => '<p><em>' . t('You lose points by selecting incorrect options. You may leave an option blank to avoid losing points.') . '</em></p>',
    );
    return $form;
  }

  /**
   * Question response validator.
   */
  public function getAnsweringFormValidate(array &$form, array &$form_state = NULL) {
    $question_answer = $form_state['values']['question'][$this->question->qid];
    foreach ($question_answer as $value) {
      if ($value != 'def') {
        return TRUE;
      }
    }
    form_set_error('', t('You need to match at least one of the items.'));
  }

  /**
   * Shuffles an array, but keeps the keys, and makes sure the first element is the default element
   *
   * @param $array
   *  Array to be shuffled
   * @return
   *  A shuffled version of the array with $array['def'] = '' as the first element
   */
  private function customShuffle(array $array = array()) {
    $new_array = array();
    $new_array['def'] = '';
    while (count($array)) {
      $element = array_rand($array);
      $new_array[$element] = $array[$element];
      unset($array[$element]);
    }
    return $new_array;
  }

  /**
   * Helper function to fetch subquestions
   *
   * @return
   *  Array with two arrays, matches and selected options
   */
  public function getSubquestions() {
    $matches = $select_option = array();
    $query = db_query('SELECT match_id, question, answer, feedback FROM {quiz_matching_question} WHERE qid = :qid AND vid = :vid', array(':qid' => $this->question->qid, ':vid' => $this->question->vid));
    while ($result = $query->fetch()) {
      $matches[] = array(
          'match_id' => $result->match_id,
          'question' => $result->question,
          'answer'   => $result->answer,
          'feedback' => $result->feedback,
      );
      $select_option[$result->match_id] = $result->answer;
    }
    return array($matches, $select_option);
  }

  /**
   * Implementation of getBodyFieldTitle
   *
   * @see QuizQuestion#getBodyFieldTitle()
   */
  public function getBodyFieldTitle() {
    return t('Instruction');
  }

  /**
   * Implementation of getCreationForm
   *
   * @see QuizQuestion#getCreationForm($form_state)
   */
  public function getCreationForm(array &$form_state = NULL) {
    // Get the nodes settings, users settings or default settings
    $default_settings = $this->getDefaultAltSettings();

    $form['settings'] = array(
        '#type'        => 'fieldset',
        '#title'       => t('Settings'),
        '#collapsible' => TRUE,
        '#collapsed'   => FALSE,
    );
    $form['settings']['choice_penalty'] = array(
        '#type'          => 'checkbox',
        '#title'         => t('Penalty for guessing'),
        '#description'   => t('Subtract 1 point from the users score for each incorrect match. Scores cannot go below 0.'),
        '#default_value' => $default_settings['choice_penalty'],
        '#parents'       => array('choice_penalty'),
    );

    $form['match'] = array(
        '#type'        => 'fieldset',
        '#title'       => t('Answer'),
        '#weight'      => -4,
        '#tree'        => TRUE,
        '#theme'       => 'matching_question_form',
        '#description' => t('Write your pairs in the question and answer columns. For the user the question will be fixed and the answers will be shown as alternatives in a dropdown box.'),
    );
    for ($i = 1; $i <= variable_get('quiz_matching_form_size', 5); ++$i) {
      $form['match'][$i] = array(
          '#type'  => 'fieldset',
          '#title' => t('Question ' . $i),
      );
      $form['match'][$i]['match_id'] = array(
          '#type'          => 'value',
          '#default_value' => isset($this->question->match[$i - 1]['match_id']) ? $this->question->match[$i - 1]['match_id'] : ''
      );
      $form['match'][$i]['question'] = array(
          '#type'          => 'textarea',
          '#rows'          => 2,
          '#default_value' => isset($this->question->match[$i - 1]['question']) ? $this->question->match[$i - 1]['question'] : '',
          '#required'      => $i < 3,
      );
      $form['match'][$i]['answer'] = array(
          '#type'          => 'textarea',
          '#rows'          => 2,
          '#default_value' => isset($this->question->match[$i - 1]['answer']) ? $this->question->match[$i - 1]['answer'] : '',
          '#required'      => $i < 3,
      );

      $form['match'][$i]['feedback'] = array(
          '#type'          => 'textarea',
          '#rows'          => 2,
          '#default_value' => isset($this->question->match[$i - 1]['feedback']) ? $this->question->match[$i - 1]['feedback'] : ''
      );
    }
    return $form;
  }

  /**
   * Helper function provding the default settings for the creation form.
   *
   * @return
   *  Array with the default settings
   */
  private function getDefaultAltSettings() {
    // If the node is being updated the default settings are those stored in the node
    if (isset($this->question->qid)) {
      $settings['choice_penalty'] = $this->question->choice_penalty;
    }
    // The user is creating his first matching node
    else {
      $settings['choice_penalty'] = 0;
    }
    return $settings;
  }

  /**
   * Implementation of getMaximumScore
   *
   * @see QuizQuestion#getMaximumScore()
   */
  public function getMaximumScore() {
    $to_return = 0;
    foreach ($this->question->match as $match) {
      if (empty($match['question']) || empty($match['answer'])) {
        continue;
      }
      $to_return++;
    }
    // The maximum score = the number of sub-questions
    return $to_return;
  }

  /**
   * Get the correct answers for this question
   *
   * @return
   *  Array of correct answers
   */
  public function getCorrectAnswer() {
    $correct_answers = array();
    $query = db_query('SELECT match_id, question, answer, feedback FROM {quiz_matching_question} WHERE qid = :qid AND vid = :vid', array(':qid' => $this->question->qid, ':vid' => $this->question->vid));
    while ($result = $query->fetch()) {
      $correct_answers[$result->match_id] = array(
          'match_id' => $result->match_id,
          'question' => $result->question,
          'answer'   => $result->answer,
          'feedback' => $result->feedback,
      );
    }
    return $correct_answers;
  }

}

/**
 * Extension of QuizQuestionResponse
 */
class MatchingResponse extends QuizQuestionResponse {

  /**
   * Constructor
   */
  public function __construct($result_id, \Drupal\quiz_question\Entity\Question $question, $answer = NULL) {
    parent::__construct($result_id, $question, $answer);
    if (!isset($answer)) {
      $res = db_query('SELECT ua.answer, score, ua.match_id FROM {quiz_matching_user_answers} ua
              JOIN {quiz_matching_question} n ON n.match_id = ua.match_id
              WHERE n.qid = :qid AND n.vid = :vid AND ua.result_id = :result_id', array(':qid' => $question->qid, ':vid' => $question->vid, ':result_id' => $result_id));
      $this->answer = array();
      while ($obj = $res->fetch()) {
        $this->answer[$obj->match_id] = $obj->answer;
      }
    }
    $this->is_correct = $this->isCorrect();
  }

  /**
   * Implementation of save
   *
   * @see QuizQuestionResponse#save()
   */
  public function save() {
    if (!isset($this->answer) || !is_array($this->answer)) {
      return;
    }
    $insert = db_insert('quiz_matching_user_answers')->fields(array('match_id', 'result_id', 'answer', 'score'));
    foreach ($this->answer as $key => $value) {
      $insert->values(array(
          'match_id'  => $key,
          'result_id' => $this->result_id,
          'answer'    => (int) $value,
          'score'     => ($key == $value) ? 1 : 0,
      ));
    }
    $insert->execute();
  }

  /**
   * Implementation of delete
   *
   * @see QuizQuestionResponse#delete()
   */
  public function delete() {
    $match_id = db_query('SELECT match_id FROM {quiz_matching_question} WHERE qid = :qid AND vid = :vid', array(':qid' => $this->question->qid, ':vid' => $this->question->vid))->fetchCol();
    db_delete('quiz_matching_user_answers')
      ->condition('match_id', is_array($match_id) ? $match_id : array(0), 'IN')
      ->condition('result_id', $this->result_id)
      ->execute();
  }

  /**
   * Implementation of score
   *
   * @see QuizQuestionResponse#score()
   */
  public function score() {
    $wrong_answer = 0;
    $correct_answer = 0;
    $user_answers = isset($this->answer['answer']) ? $this->answer['answer'] : $this->answer;
    $MatchingQuestion = new MatchingQuestion($this->question);
    $correct_answers = $MatchingQuestion->getCorrectAnswer();
    foreach ((array) $user_answers as $key => $value) {
      if ($value != 0 && $correct_answers[$key]['answer'] == $correct_answers[$value]['answer']) {
        $correct_answer++;
      }
      elseif ($value == 0 || $value == 'def') {

      }
      else {
        $wrong_answer++;
      }
    }

    $score = $correct_answer;
    if ($this->question->choice_penalty) {
      $score -= $wrong_answer;
    }

    return $score < 0 ? 0 : $score;
  }

  /**
   * Implementation of getResponse
   *
   * @see QuizQuestionResponse#getResponse()
   */
  public function getResponse() {
    return $this->answer;
  }

  /**
   * Implementation of getReportFormResponse
   */
  public function getReportFormResponse() {
    $data = array();
    $answers = $this->question->answers[0]['answer'];
    $solution = $this->quizQuestion->getSubquestions();
    foreach ($this->question->match as $match) {

      $data[] = array(
          'choice'            => $match['question'],
          'attempt'           => !empty($answers[$match['match_id']]) ? $solution[1][$answers[$match['match_id']]] : '',
          'correct'           => $answers[$match['match_id']] == $match['match_id'] ? theme('quiz_answer_result', array('type' => 'correct')) : theme('quiz_answer_result', array('type' => 'incorrect')),
          'score'             => $answers[$match['match_id']] == $match['match_id'] ? 1 : 0,
          'answer_feedback'   => $match['feedback'],
          'question_feedback' => 'Question feedback',
          'solution'          => $solution[1][$match['match_id']],
          'quiz_feedback'     => t('@quiz feedback', array('@quiz' => QUIZ_NAME)),
      );
    }

    return $data;
  }

}
